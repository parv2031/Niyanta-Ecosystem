"""
power_grid_env.py  v4  —  SAC Gymnasium wrapper for the C++ Power Grid simulator

Key fixes and improvements over the previous continuous version:
  ┌──────────────────────────────────────────────────────────────────────────┐
  │ 1. No "ping" command  — C++ bridge has no ping handler; obs_dim is now  │
  │    discovered dynamically from the first reset() reply.                 │
  │ 2. Persistent connection — ZMQ socket is reused across episodes;        │
  │    reconnect only happens on actual ZMQ failure (no more per-reset      │
  │    socket teardown that breaks REQ/REP state).                          │
  │ 3. Python-side time features — C++ build_obs() doesn't emit them; we   │
  │    compute 8 cyclic features from the Python csv_row counter and        │
  │    append them after parsing the C++ payload.                           │
  │ 4. n_subs is a constructor parameter — the C++ substation count is user │
  │    configurable (default UI shows 100); hardcoding 50 causes a wrong    │
  │    edge/sub split in every obs.                                         │
  │ 5. Action clipping + auto-resize — replaces the crashing assertion.     │
  │ 6. Episode termination guard — truncated=True at YEAR_HOURS-1 steps    │
  │    so C++ never reads demand_schedule[8760] (one past the last row).   │
  │ 7. Metrics from obs — n_overloaded / avg_util / max_severity extracted  │
  │    from edge-util prefix of obs (C++ reply has no info dict).           │
  │ 8. Auto-reconnect on ZMQ error — broken REQ socket is rebuilt cleanly. │
  └──────────────────────────────────────────────────────────────────────────┘

Action layout
─────────────
  [ p_factors (n_subs) | edge_scores (n_edges) ]  ∈ [0, 1]

  p_factors[i]    demand-scaling for substation i  (1.0 = full, 0.0 = shed all)
  edge_scores[i]  > 0.5 → connect edge i,  ≤ 0.5 → disconnect

Observation layout
──────────────────
  [ edge_utils (n_edges) | sub_demands (n_subs) | time_feats (8) ]

  edge_utils[i]   signed current_load / max_load  (negative = reversed flow)
  sub_demands[i]  current_demand / max_limit
  time_feats      8 cyclic features for hour-of-day / day-of-week /
                  month / week-of-year  (all ∈ [-1, 1])
"""

from __future__ import annotations

import json
import math
from typing import Optional, Tuple

import numpy as np
import zmq
import gymnasium as gym
from gymnasium import spaces

# ── Module-level constants ─────────────────────────────────────────────────
N_TIME_FEATS    = 8
ZMQ_ENDPOINT    = "tcp://localhost:5556"
ZMQ_TIMEOUT_MS  = 15_000
YEAR_HOURS      = 8_760
# C++ does  csv_row_++ then demand_schedule[csv_row_].
# Valid indices are 0 … 8759.  We truncate after step 8759 so Python never
# triggers a step that would request demand_schedule[8760].
EPISODE_STEPS   = YEAR_HOURS - 1
OVERLOAD_WINDOW = 24
OBS_UPPER_BOUND = 600   # generous placeholder until real n_edges is known


class PowerGridEnv(gym.Env):
    """Continuous-action Gymnasium environment bridged to the C++ Power Grid."""

    metadata = {"render_modes": ["human"]}

    def __init__(
        self,
        n_subs:     int = 100,           # ← MUST match C++ substation count
        endpoint:   str = ZMQ_ENDPOINT,
        timeout_ms: int = ZMQ_TIMEOUT_MS,
    ):
        super().__init__()
        self.n_subs      = n_subs
        self._endpoint   = endpoint
        self._timeout    = timeout_ms

        # Lazily set once obs_dim is known from the first reset() reply
        self._n_edges    : Optional[int] = None
        self._obs_dim    : Optional[int] = None
        self._action_dim : Optional[int] = None

        # ZMQ state
        self._ctx    = zmq.Context()
        self._socket : Optional[zmq.Socket] = None
        self._connected = False

        # Episode state
        self._csv_row          = 0
        self._step_count       = 0
        self._overload_history : list[int] = []

        # Spaces are placeholders until _update_spaces() is called
        self._init_placeholder_spaces()

    # ── Space management ───────────────────────────────────────────────────

    def _init_placeholder_spaces(self):
        """Conservative placeholder spaces; replaced after first reset()."""
        n_e  = OBS_UPPER_BOUND
        odim = n_e + self.n_subs + N_TIME_FEATS
        adim = self.n_subs + n_e

        low  = np.full(odim, -2.0, dtype=np.float32)
        high = np.full(odim,  2.0, dtype=np.float32)
        low[-N_TIME_FEATS:]  = -1.0
        high[-N_TIME_FEATS:] =  1.0

        self.observation_space = spaces.Box(low=low, high=high, dtype=np.float32)
        self.action_space      = spaces.Box(0.0, 1.0, (adim,), dtype=np.float32)

    def _update_spaces(self, n_edges: int):
        """Called once, when n_edges is discovered from the reset() reply."""
        self._n_edges    = n_edges
        self._obs_dim    = n_edges + self.n_subs + N_TIME_FEATS
        self._action_dim = self.n_subs + n_edges

        low  = np.full(self._obs_dim, -2.0, dtype=np.float32)
        high = np.full(self._obs_dim,  2.0, dtype=np.float32)
        low[-N_TIME_FEATS:]  = -1.0
        high[-N_TIME_FEATS:] =  1.0

        self.observation_space = spaces.Box(low=low, high=high, dtype=np.float32)
        self.action_space      = spaces.Box(0.0, 1.0, (self._action_dim,), dtype=np.float32)
        print(
            f"[Env] Spaces updated — obs_dim={self._obs_dim}  "
            f"n_edges={n_edges}  action_dim={self._action_dim}  "
            f"n_subs={self.n_subs}"
        )

    # ── ZMQ helpers ────────────────────────────────────────────────────────

    def _connect(self):
        """Build (or rebuild) the ZMQ REQ socket."""
        if self._socket is not None:
            try:
                self._socket.close()
            except Exception:
                pass

        self._socket = self._ctx.socket(zmq.REQ)
        self._socket.setsockopt(zmq.RCVTIMEO, self._timeout)
        self._socket.setsockopt(zmq.LINGER,    0)
        self._socket.connect(self._endpoint)
        self._connected = True
        print(f"[Env] ZMQ socket connected → {self._endpoint}")

    def _send(self, payload: dict, max_retries: int = 5) -> dict:
        """
        Send a JSON request and receive a JSON reply.

        Implements exponential backoff for initial connections and timeouts,
        which is vital during container startup when the C++ simulator might
        still be building the network graph or loading data.
        """
        import time
        retries = 0
        backoff = 1.0

        while True:
            try:
                self._socket.send(json.dumps(payload).encode())
                return json.loads(self._socket.recv().decode())
            except zmq.error.Again:
                retries += 1
                if retries >= max_retries:
                    raise TimeoutError(
                        f"[Env] C++ sim did not reply after {max_retries} attempts "
                        f"({self._timeout} ms each). Ensure simulator.cpp is running."
                    )
                print(f"[Env] ZMQ timeout (attempt {retries}/{max_retries}). "
                      f"Waiting {backoff}s before retrying...")
                
                # ZMQ REQ sockets must be rebuilt after a timeout before retrying
                self._connected = False
                self._connect()
                
                time.sleep(backoff)
                backoff = min(backoff * 2.0, 10.0)
            except Exception as exc:
                print(f"[Env] ZMQ error ({exc!r}) — rebuilding socket…")
                self._connected = False
                self._connect()
                raise RuntimeError(f"[Env] Socket reset after error: {exc}") from exc

    # ── Observation helpers ────────────────────────────────────────────────

    def _time_features(self) -> np.ndarray:
        """
        8 cyclic features derived from the Python-side csv_row counter.
        One CSV row ≈ one hour.
        """
        r     = self._csv_row
        hour  =  r % 24
        dow   = (r // 24)  % 7
        month = (r // 720) % 12    # ~720 hours per month
        week  = (r // 168) % 52    # ~168 hours per week

        τ = 2.0 * math.pi
        return np.array([
            math.sin(τ * hour  / 24.0),
            math.cos(τ * hour  / 24.0),
            math.sin(τ * dow   /  7.0),
            math.cos(τ * dow   /  7.0),
            math.sin(τ * month / 12.0),
            math.cos(τ * month / 12.0),
            math.sin(τ * week  / 52.0),
            math.cos(τ * week  / 52.0),
        ], dtype=np.float32)

    def _parse_obs(self, raw: list) -> np.ndarray:
        """
        Parse the C++ obs payload (no time features) and append Python time features.
        Also triggers _update_spaces() on the first call.
        """
        arr = np.asarray(raw, dtype=np.float32)

        # ── First-time discovery ──────────────────────────────────────
        if self._n_edges is None:
            n_edges = max(0, len(arr) - self.n_subs)
            if n_edges == 0 and len(arr) < self.n_subs:
                raise ValueError(
                    f"[Env] C++ obs has only {len(arr)} values, "
                    f"expected at least n_subs={self.n_subs}. "
                    "Check that n_subs matches the C++ simulation config."
                )
            self._update_spaces(n_edges)

        # ── Align to expected raw length (n_edges + n_subs) ──────────
        raw_len = self._n_edges + self.n_subs
        if len(arr) < raw_len:
            arr = np.pad(arr, (0, raw_len - len(arr)))
        elif len(arr) > raw_len:
            arr = arr[:raw_len]

        return np.concatenate([arr, self._time_features()])

    def _metrics_from_obs(self, obs: np.ndarray) -> dict:
        """
        Extract grid-health metrics directly from the observation vector.
        Edge-utilisation ratios occupy obs[:n_edges].
        """
        if self._n_edges is None or self._n_edges == 0:
            return {}

        utils = np.abs(obs[:self._n_edges])
        return {
            "n_overloaded": int((utils > 1.0).sum()),
            "avg_util":     float(utils.mean()),
            "max_severity": float(utils.max()),
        }

    # ── Gymnasium API ──────────────────────────────────────────────────────

    def reset(
        self,
        *,
        seed    = None,
        options = None,
    ) -> Tuple[np.ndarray, dict]:
        super().reset(seed=seed)

        if not self._connected:
            self._connect()

        reply = self._send({"cmd": "reset"})

        self._csv_row          = 0
        self._step_count       = 0
        self._overload_history = []

        obs  = self._parse_obs(reply.get("obs", []))
        info = {
            "csv_row": 0,
            "step":    0,
            **self._metrics_from_obs(obs),
        }
        return obs, info

    def step(
        self,
        action: np.ndarray,
    ) -> Tuple[np.ndarray, float, bool, bool, dict]:
        # ── Sanitise action ───────────────────────────────────────────
        action = np.asarray(action, dtype=np.float32).flatten()

        if self._action_dim is not None:
            if len(action) < self._action_dim:
                # Pad missing edge scores with 1.0 (keep all edges connected)
                action = np.pad(
                    action, (0, self._action_dim - len(action)),
                    constant_values=1.0,
                )
            elif len(action) > self._action_dim:
                action = action[:self._action_dim]

        action = np.clip(action, 0.0, 1.0)

        p    = action[:self.n_subs].tolist()
        edge = action[self.n_subs:].tolist()

        # ── Step the C++ sim ──────────────────────────────────────────
        reply = self._send({"cmd": "step", "p": p, "edge": edge})

        self._csv_row   += 1
        self._step_count += 1

        obs     = self._parse_obs(reply.get("obs", []))
        reward  = float(reply.get("reward", 0.0))
        # print(reward)
        # print("/n")
        metrics = self._metrics_from_obs(obs)

        # ── 24-h sustained-overload penalty (Python-side) ────────────
        # n_over = metrics.get("n_overloaded", 0)
        # self._overload_history.append(n_over)
        # if len(self._overload_history) > OVERLOAD_WINDOW:
        #     self._overload_history.pop(0)
        # if (
        #     len(self._overload_history) == OVERLOAD_WINDOW
        #     and all(h > 0 for h in self._overload_history)
        # ):
        #     reward -= 5.0
        #     metrics["regional_overfit_penalty"] = True

        # ── Power-shed proxy ─────────────────────────────────────────
        # Measures how far the agent has pulled demand away from 100 %.
        # Real shed (MW) = sum_i(demand_i * (1 - p_i)); we use a relative proxy.
        avg_p = float(np.mean(action[:self.n_subs]))
        metrics["power_shed_est"] = round((1.0 - avg_p) * 100.0 * self.n_subs, 1)

        terminated = bool(reply.get("done", False))
        # Guard: prevent C++ from indexing demand_schedule[YEAR_HOURS]
        truncated  = self._csv_row >= EPISODE_STEPS

        info = {
            "csv_row": self._csv_row,
            "step":    self._step_count,
            **metrics,
        }

        # --- Niyanta TakshakDB Broadcast Hook ---
        try:
            import socket
            import os
            if not hasattr(self, '_t_sock'):
                self._t_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                host = os.environ.get("TAKSHAK_HOST", "127.0.0.1")
                self._t_sock.connect((host, 6379))
            
            payload = json.dumps({
                "obs": obs.tolist(),
                "reward": reward,
                "step": self._step_count,
                "metrics": metrics
            }, separators=(',', ':'))
            self._t_sock.sendall(f"PUBLISH rl_telemetry {payload}\r\n".encode())
        except Exception as e:
            self._t_sock = None # Will try reconnecting next step if failed
        
        return obs, reward, terminated, truncated, info

    # ──────────────────────────────────────────────────────────────────────

    def close(self):
        if self._socket:
            try:
                self._socket.close()
            except Exception:
                pass
        try:
            self._ctx.term()
        except Exception:
            pass
        self._connected = False

    def render(self):
        pass   # Raylib visualisation is owned by the C++ process