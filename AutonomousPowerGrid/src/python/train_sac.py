"""
train_sac.py  v1  —  Soft Actor-Critic for continuous Power Grid control

Architecture overview
─────────────────────
  SquashedGaussianActor
    • Split obs encoder: grid_tower (edge_utils + sub_demands) + time_tower
    • Separate mean/log-std heads for p_factors (n_subs) and edge_scores (n_edges)
    • Tanh squashing shifted to [0, 1]:  a = (tanh(u) + 1) / 2
    • Reparameterisation trick for gradient flow through stochastic samples

  TwinCritic
    • Two independent Q(s, a) networks — clipped double-Q to reduce
      over-estimation bias
    • Soft target copies updated via exponential moving average (τ = 0.005)

  Auto-tuned entropy temperature α
    • Target entropy H* = −action_dim × entropy_scale
    • log α updated to keep  E[−log π(a|s)] ≈ H*

Why SAC for this problem
─────────────────────────
  The action space is large (~225 continuous dims) and the reward is dense.
  SAC's entropy regularisation naturally prevents premature convergence to
  a sub-optimal deterministic policy (e.g. "always throttle everything")
  without needing a manually decayed epsilon schedule.

Replay buffer
─────────────
  Standard uniform circular buffer (no PER).  SAC's entropy term already
  drives broad exploration, so the priority correction that PER offers is
  less critical here.

Usage
─────
  python train_sac.py                            # 200-episode training
  python train_sac.py --episodes 500 --n_subs 100
  python train_sac.py --resume checkpoints_sac/best_sac.pt
  python train_sac.py --eval   --resume checkpoints_sac/best_sac.pt
"""

from __future__ import annotations

import argparse
import math
import os
import random
import time
from dataclasses import dataclass, field
from typing import List, Optional, Tuple

import numpy as np
import torch
import torch.nn as nn
import torch.nn.functional as F
import torch.optim as optim
from torch.distributions import Normal

from power_grid_env import PowerGridEnv, N_TIME_FEATS

# ── Hyper-parameters ──────────────────────────────────────────────────────────

@dataclass
class HParams:
    # ── Environment ───────────────────────────────────────────────────
    n_subs:        int   = 100         # must match C++ substation count

    # ── Network ───────────────────────────────────────────────────────
    hidden_dim:    int   = 256

    # ── Optimisation ──────────────────────────────────────────────────
    actor_lr:      float = 1e-4        # restored; warmup diversity now protects actor
    critic_lr:     float = 3e-4
    alpha_lr:      float = 0            # 0 = freeze α; set >0 only after actor stabilises
    gamma:         float = 0.99
    tau:           float = 0.005       # soft target-update rate
    grad_clip:     float = 1.0
    reward_scale:  float = 0.05      # normalise C++ rewards (~100 scale) to ~1
    alpha_min: float = 0.02     # fixed α when alpha_lr=0 (pin at 0.02)
    alpha_max: float = 0.02     # same as min → α is frozen

    # ── SAC entropy ───────────────────────────────────────────────────
    # target_entropy = −action_dim × entropy_scale
    # With scale=0.5 the policy is encouraged to stay moderately stochastic.
    entropy_scale: float = 0.005       # kept for target_entropy calc; irrelevant when alpha_lr=0

    # ── Replay buffer ─────────────────────────────────────────────────
    replay_capacity: int  = 200_000
    min_replay:      int  = 26_280      # steps before gradient updates start
    batch_size:      int  = 256
    train_freq:      int  = 1          # gradient update every N env steps
    warmup_steps:    int  = 26_280      # fully-random actions during warmup

    # ── Logging / checkpointing ───────────────────────────────────────
    log_freq:      int   = 1           # log every episode
    save_dir:      str   = "checkpoints_sac"
    seed:          int   = 42

HP = HParams()


# ── Replay Buffer ─────────────────────────────────────────────────────────────

class ReplayBuffer:
    """
    Circular replay buffer backed by pre-allocated NumPy arrays.

    Stores rewards *already scaled* by reward_scale so that the Bellman
    targets are in a stable range for the critic.
    """

    def __init__(self, capacity: int, obs_dim: int, action_dim: int):
        self.capacity  = capacity
        self._ptr      = 0
        self._size     = 0

        self._obs     = np.zeros((capacity, obs_dim),    dtype=np.float32)
        self._nobs    = np.zeros((capacity, obs_dim),    dtype=np.float32)
        self._actions = np.zeros((capacity, action_dim), dtype=np.float32)
        self._rewards = np.zeros((capacity, 1),          dtype=np.float32)
        self._dones   = np.zeros((capacity, 1),          dtype=np.float32)

    def push(
        self,
        obs:      np.ndarray,
        action:   np.ndarray,
        reward:   float,
        next_obs: np.ndarray,
        done:     bool,
    ):
        i = self._ptr
        self._obs    [i] = obs
        self._nobs   [i] = next_obs
        self._actions[i] = action
        self._rewards[i] = reward
        self._dones  [i] = float(done)
        self._ptr  = (i + 1) % self.capacity
        self._size = min(self._size + 1, self.capacity)

    def sample(
        self, batch_size: int, device: torch.device
    ) -> Tuple[torch.Tensor, ...]:
        idx = np.random.randint(0, self._size, size=batch_size)
        def t(x): return torch.as_tensor(x[idx], dtype=torch.float32, device=device)
        return t(self._obs), t(self._actions), t(self._rewards), \
               t(self._nobs), t(self._dones)

    def __len__(self) -> int:
        return self._size


# ── Networks ──────────────────────────────────────────────────────────────────

class SquashedGaussianActor(nn.Module):
    """
    Gaussian actor with tanh squashing mapped to [0, 1].

    Sampling:
        u      ~ N(mean, std)
        a_tanh = tanh(u)                         ∈ (-1, 1)
        a      = (a_tanh + 1) / 2                ∈ (0, 1)

    Log-probability Jacobian correction (per dimension):
        log|da/du| = log(0.5) + log(1 - tanh²(u))
        log π(a|s) = Σ_i [ log N(u_i; μ_i, σ_i) - log|da_i/du_i| ]
    """

    LOG_STD_MIN = -5.0
    LOG_STD_MAX =  2.0

    def __init__(
        self,
        obs_dim:    int,
        n_subs:     int,
        n_edges:    int,
        hidden_dim: int = 256,
    ):
        super().__init__()
        self.n_subs  = n_subs
        self.n_edges = n_edges
        grid_dim     = obs_dim - N_TIME_FEATS   # edge_utils + sub_demands

        # ── Shared encoder ────────────────────────────────────────────
        self.grid_tower = nn.Sequential(
            nn.Linear(grid_dim,   hidden_dim), nn.LayerNorm(hidden_dim), nn.SiLU(),
            nn.Linear(hidden_dim, hidden_dim), nn.LayerNorm(hidden_dim), nn.SiLU(),
            nn.Linear(hidden_dim, hidden_dim),                           nn.SiLU(),
        )
        self.time_tower = nn.Sequential(
            nn.Linear(N_TIME_FEATS, 32), nn.SiLU(),
            nn.Linear(32,           32), nn.SiLU(),
        )
        combined = hidden_dim + 32

        # ── Separate heads for the two action sub-spaces ──────────────
        # p_factors (demand scaling)
        self.p_mean   = nn.Linear(combined, n_subs)
        self.p_logstd = nn.Linear(combined, n_subs)
        # edge_scores (topology control)
        self.e_mean   = nn.Linear(combined, n_edges)
        self.e_logstd = nn.Linear(combined, n_edges)

        self._init_weights()

    def _init_weights(self):
        # p_factors: bias=2.0 → tanh(2)=0.964 → a=0.982 → serve ~98% from step 1
        nn.init.uniform_(self.p_mean.weight, -3e-3, 3e-3)
        nn.init.constant_(self.p_mean.bias, 1.5)
        # edge_scores: same → keep ~98% of edges connected from step 1
        nn.init.uniform_(self.e_mean.weight, -3e-3, 3e-3)
        nn.init.constant_(self.e_mean.bias, 0.85)
        # log_std: bias=-1.0 → std=0.37 → less random, stays near the bias point
        nn.init.constant_(self.p_logstd.bias, -2.0)
        nn.init.constant_(self.e_logstd.bias, -2.0)

    # ──────────────────────────────────────────────────────────────────

    def _encode(self, obs: torch.Tensor) -> torch.Tensor:
        g = self.grid_tower(obs[:, :-N_TIME_FEATS])
        t = self.time_tower(obs[:,  -N_TIME_FEATS:])
        return torch.cat([g, t], dim=-1)

    @staticmethod
    def _squash_sample(
        mean:          torch.Tensor,
        log_std:       torch.Tensor,
        deterministic: bool,
    ) -> Tuple[torch.Tensor, torch.Tensor]:
        """
        Returns (action ∈ [0,1], log_prob_per_dim).
        Uses the reparameterisation trick; log_prob includes Jacobian correction.
        """
        std    = log_std.exp()
        dist   = Normal(mean, std)
        u      = mean if deterministic else dist.rsample()
        a_tanh = torch.tanh(u)
        a      = (a_tanh + 1.0) * 0.5

        # log|da/du| = log(0.5) + log(1 − tanh²(u))
        # Numerically stable: clamp argument away from 0
        log_jacob = (
            math.log(0.5)
            + torch.log(torch.clamp(1.0 - a_tanh.pow(2), min=1e-6))
        )
        log_prob = dist.log_prob(u) - log_jacob   # (B, dim)
        return a, log_prob

    def forward(
        self,
        obs:           torch.Tensor,
        deterministic: bool = False,
    ) -> Tuple[torch.Tensor, torch.Tensor]:
        """
        Returns
        -------
        action   : (B, n_subs + n_edges)  ∈ [0, 1]
        log_prob : (B, 1)   — summed across action dimensions
        """
        h = self._encode(obs)

        p_lgs = self.p_logstd(h).clamp(self.LOG_STD_MIN, self.LOG_STD_MAX)
        e_lgs = self.e_logstd(h).clamp(self.LOG_STD_MIN, self.LOG_STD_MAX)

        p_a, p_lp = self._squash_sample(self.p_mean(h), p_lgs, deterministic)
        e_a, e_lp = self._squash_sample(self.e_mean(h), e_lgs, deterministic)

        action   = torch.cat([p_a, e_a], dim=-1)
        # Use .mean() not .sum() — with 419 dims, sum ≈ 1000 overwhelms α×Q
        # mean gives log_prob ≈ 3.3/dim, so α×log_p ≈ 0.07 << Q-values (~15)
        log_prob = torch.cat([p_lp, e_lp], dim=-1).mean(-1, keepdim=True)
        # log_prob = p_lp.sum(-1, keepdim=True)
        # log_prob = torch.zeros(obs.shape[0], 1, dtype=obs.dtype, device=obs.device)
        return action, log_prob

    @torch.no_grad()
    def get_action(
        self,
        obs_np:        np.ndarray,
        deterministic: bool = False,
    ) -> np.ndarray:
        dev = next(self.parameters()).device
        obs_t = torch.as_tensor(obs_np, dtype=torch.float32, device=dev)
        if obs_t.ndim == 1:
            obs_t = obs_t.unsqueeze(0)
        a, _ = self.forward(obs_t, deterministic=deterministic)
        return a.squeeze(0).cpu().numpy()


class TwinCritic(nn.Module):
    """
    Two Q(s, a) networks.  Taking min(Q1, Q2) reduces over-estimation bias.

    The observation and action are concatenated before the first layer.
    Using LayerNorm on the first layer stabilises training when obs/action
    magnitudes vary across dimensions (edge-utils can be >1 during overloads).
    """

    def __init__(self, obs_dim: int, action_dim: int, hidden_dim: int = 256):
        super().__init__()
        in_dim = obs_dim + action_dim

        def _q():
            return nn.Sequential(
                nn.Linear(in_dim,     hidden_dim), nn.LayerNorm(hidden_dim), nn.SiLU(),
                nn.Linear(hidden_dim, hidden_dim),                           nn.SiLU(),
                nn.Linear(hidden_dim, hidden_dim // 2),                      nn.SiLU(),
                nn.Linear(hidden_dim // 2, 1),
            )

        self.q1 = _q()
        self.q2 = _q()

    def forward(
        self, obs: torch.Tensor, action: torch.Tensor
    ) -> Tuple[torch.Tensor, torch.Tensor]:
        x = torch.cat([obs, action], dim=-1)
        return self.q1(x), self.q2(x)

    def q_min(self, obs: torch.Tensor, action: torch.Tensor) -> torch.Tensor:
        q1, q2 = self.forward(obs, action)
        return torch.min(q1, q2)


# ── SAC Agent ─────────────────────────────────────────────────────────────────

class SACAgent:
    """
    Full SAC agent: actor + twin-critic + auto-tuned temperature α.

    Update order per call to learn():
        1. Critic update (TD-backup with entropy bonus)
        2. Actor update  (maximise soft Q-value)
        3. Alpha update  (minimise |entropy − H*|)
        4. Soft target-critic update  (EMA, τ = 0.005)
    """

    def __init__(
        self,
        obs_dim:    int,
        n_subs:     int,
        n_edges:    int,
        device:     torch.device,
    ):
        self.device     = device
        self.obs_dim    = obs_dim
        self.n_subs     = n_subs
        self.n_edges    = n_edges
        self.action_dim = n_subs + n_edges
        self._step      = 0

        # ── Networks ──────────────────────────────────────────────────
        self.actor  = SquashedGaussianActor(
            obs_dim, n_subs, n_edges, HP.hidden_dim
        ).to(device)
        self.critic = TwinCritic(obs_dim, self.action_dim, HP.hidden_dim).to(device)
        self.target = TwinCritic(obs_dim, self.action_dim, HP.hidden_dim).to(device)
        self.target.load_state_dict(self.critic.state_dict())
        for p in self.target.parameters():
            p.requires_grad = False

        # ── Optimisers ────────────────────────────────────────────────
        self.actor_opt  = optim.Adam(self.actor.parameters(),  lr=HP.actor_lr)
        self.critic_opt = optim.Adam(self.critic.parameters(), lr=HP.critic_lr)

        # ── Auto-tuned entropy temperature ────────────────────────────
        # H* = −action_dim × entropy_scale
        # With a large action space the entropy target is very negative;
        # entropy_scale tunes how deterministic the converged policy is.
        self.target_entropy = -self.action_dim * HP.entropy_scale
        self.log_alpha      = torch.zeros(1, requires_grad=True, device=device)
        self.alpha_opt      = optim.Adam([self.log_alpha], lr=HP.alpha_lr)

        # ── Replay buffer ─────────────────────────────────────────────
        self.buffer = ReplayBuffer(HP.replay_capacity, obs_dim, self.action_dim)

    # ── Public helpers ────────────────────────────────────────────────────

    @property
    def alpha(self) -> float:
        return float(self.log_alpha.exp().item())

    def select_action(
        self, obs: np.ndarray, deterministic: bool = False
    ) -> np.ndarray:
        return self.actor.get_action(obs, deterministic=deterministic)

    def random_action(self) -> np.ndarray:
        # Beta(9,1) peaks near 1.0 — fills buffer with high-service transitions.
        # Critic learns Q(obs, p≈0.9, edge≈0.9) before actor training begins.
        p_part = np.random.beta(9, 1, self.n_subs).astype(np.float32)
        e_part = np.random.beta(4, 1, self.n_edges).astype(np.float32)
        return np.concatenate([p_part, e_part])

    def push(
        self,
        obs:      np.ndarray,
        action:   np.ndarray,
        reward:   float,
        next_obs: np.ndarray,
        done:     bool,
    ):
        self.buffer.push(obs, action, reward * HP.reward_scale, next_obs, done)
        self._step += 1

    # ── Core SAC update ───────────────────────────────────────────────────

    def learn(self) -> Tuple[Optional[float], Optional[float], Optional[float]]:
        """
        Returns (critic_loss, actor_loss, alpha_loss) or (None, None, None)
        when the buffer is too small or train_freq condition is not met.
        """
        if len(self.buffer) < HP.min_replay:
            return None, None, None
        if self._step % HP.train_freq != 0:
            return None, None, None

        obs, act, rew, nobs, done = self.buffer.sample(HP.batch_size, self.device)

        # ── 1. Critic ─────────────────────────────────────────────────
        with torch.no_grad():
            next_a, next_lp = self.actor(nobs)
            q_next  = self.target.q_min(nobs, next_a)
            # Entropy-regularised Bellman backup
            backup  = rew + HP.gamma * (1.0 - done) * (
                q_next - self.log_alpha.exp() * next_lp
            )

        q1, q2 = self.critic(obs, act)
        critic_loss = F.mse_loss(q1, backup) + F.mse_loss(q2, backup)

        self.critic_opt.zero_grad(set_to_none=True)
        critic_loss.backward()
        nn.utils.clip_grad_norm_(self.critic.parameters(), HP.grad_clip)
        self.critic_opt.step()

        # ── 2. Actor ──────────────────────────────────────────────────
        new_a, log_p = self.actor(obs)
        q_val = self.critic.q_min(obs, new_a)
        # Maximise soft value = Q(s,a) − α · log π(a|s)
        actor_loss = (self.log_alpha.exp().detach() * log_p - q_val).mean()

        self.actor_opt.zero_grad(set_to_none=True)
        actor_loss.backward()
        nn.utils.clip_grad_norm_(self.actor.parameters(), HP.grad_clip)
        self.actor_opt.step()

        # ── 3. Alpha (entropy temperature) ───────────────────────────
        # Gradient pushes α so that  E[−log π(a|s)] → H*
        alpha_loss = -(
            self.log_alpha * (log_p.detach() + self.target_entropy)
        ).mean()

        self.alpha_opt.zero_grad(set_to_none=True)
        alpha_loss.backward()
        self.alpha_opt.step()
        with torch.no_grad():
            # self.log_alpha.clamp_(min=math.log(HP.alpha_min))
            self.log_alpha.clamp_(min=math.log(HP.alpha_min),max=math.log(HP.alpha_max))
        # ── 4. Soft target update ─────────────────────────────────────
        with torch.no_grad():
            for po, pt in zip(self.critic.parameters(), self.target.parameters()):
                pt.data.mul_(1.0 - HP.tau).add_(HP.tau * po.data)

        return (
            float(critic_loss.item()),
            float(actor_loss.item()),
            float(alpha_loss.item()),
        )

    # ── Checkpoint helpers ────────────────────────────────────────────────

    def save(self, path: str):
        torch.save({
            "actor":       self.actor.state_dict(),
            "critic":      self.critic.state_dict(),
            "target":      self.target.state_dict(),
            "actor_opt":   self.actor_opt.state_dict(),
            "critic_opt":  self.critic_opt.state_dict(),
            "log_alpha":   self.log_alpha.data.clone(),
            "alpha_opt":   self.alpha_opt.state_dict(),
            "step":        self._step,
            # Saved so a resumed run can validate dims match
            "obs_dim":     self.obs_dim,
            "n_subs":      self.n_subs,
            "n_edges":     self.n_edges,
        }, path)
        print(f"[Save] {path}")

    def load(self, path: str):
        ck = torch.load(path, map_location=self.device)

        # Sanity-check dims if available
        if "n_subs" in ck and ck["n_subs"] != self.n_subs:
            raise ValueError(
                f"Checkpoint n_subs={ck['n_subs']} ≠ current n_subs={self.n_subs}"
            )
        if "n_edges" in ck and ck["n_edges"] != self.n_edges:
            raise ValueError(
                f"Checkpoint n_edges={ck['n_edges']} ≠ current n_edges={self.n_edges}"
            )

        self.actor.load_state_dict(ck["actor"])
        self.critic.load_state_dict(ck["critic"])
        self.target.load_state_dict(ck["target"])
        self.actor_opt.load_state_dict(ck["actor_opt"])
        self.critic_opt.load_state_dict(ck["critic_opt"])
        self.log_alpha.data = ck["log_alpha"].to(self.device)
        self.alpha_opt.load_state_dict(ck["alpha_opt"])
        self._step = ck["step"]
        print(f"[Load] {path}  step={self._step}")


# ── Logger ────────────────────────────────────────────────────────────────────

class MetricsLogger:
    """
    CSV + console logger.  Columns align with the DQN logger for easy
    side-by-side comparison.
    """

    _HEADER = (
        "episode,total_reward,steps,overloads_total,max_severity,"
        "avg_util_mean,power_shed_est,alpha,"
        "critic_loss_mean,actor_loss_mean,total_steps\n"
    )

    def __init__(self, log_dir: str = "logs_sac"):
        os.makedirs(log_dir, exist_ok=True)
        self._path = os.path.join(log_dir, f"run_{int(time.time())}.csv")
        with open(self._path, "w") as f:
            f.write(self._HEADER)

    def log(
        self,
        ep:          int,
        reward:      float,
        steps:       int,
        overloads:   int,
        max_sev:     float,
        avg_util:    float,
        shed:        float,
        alpha:       float,
        c_loss:      float,
        a_loss:      float,
        total_steps: int,
    ):
        row = (
            f"{ep},{reward:.2f},{steps},{overloads},{max_sev:.4f},"
            f"{avg_util:.4f},{shed:.1f},{alpha:.5f},"
            f"{c_loss:.6f},{a_loss:.6f},{total_steps}\n"
        )
        with open(self._path, "a") as f:
            f.write(row)

        print(
            f"[Ep {ep:4d}] R={reward:9.1f}  steps={steps}  "
            f"ovrld={overloads}  maxSev={max_sev:.2f}  "
            f"avgUtil={avg_util:.2f}  shed={shed:.0f}  "
            f"α={alpha:.4f}  "
            f"cLoss={c_loss:.5f}  aLoss={a_loss:.5f}"
        )


# ── Training loop ─────────────────────────────────────────────────────────────

def train(
    n_episodes: int = 200,
    resume:     Optional[str] = None,
    eval_only:  bool = False,
):
    """
    Main training loop.

    Warm-up phase
    ─────────────
    For the first HP.warmup_steps environment steps, actions are sampled
    uniformly at random.  This pre-populates the buffer with diverse
    transitions before gradient updates begin.

    After warm-up
    ─────────────
    SAC's entropy regularisation drives continued exploration without a
    decaying epsilon schedule.  The auto-tuned α adjusts so that the
    policy maintains approximately H* = −action_dim × entropy_scale nats
    of entropy.
    """
    random.seed(HP.seed)
    np.random.seed(HP.seed)
    torch.manual_seed(HP.seed)
    device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    print(f"[Train] device={device}")

    # ── Create environment and discover dims ──────────────────────────
    env = PowerGridEnv(n_subs=HP.n_subs)
    print("[Train] Connecting to C++ simulator… (ensure it is running)")
    obs, info = env.reset()     # ← triggers obs_dim / action_dim discovery

    obs_dim    = env.observation_space.shape[0]
    action_dim = env.action_space.shape[0]
    n_edges    = env._n_edges or 0

    print(
        f"[Train] obs_dim={obs_dim}  action_dim={action_dim}  "
        f"n_subs={HP.n_subs}  n_edges={n_edges}"
    )

    # ── Agent, logger, checkpoint dir ────────────────────────────────
    agent  = SACAgent(obs_dim, HP.n_subs, n_edges, device)
    logger = MetricsLogger()
    os.makedirs(HP.save_dir, exist_ok=True)

    if resume:
        agent.load(resume)
        # Patch actor lr (checkpoint may have stored lr=0 or old value)
        for pg in agent.actor_opt.param_groups:
            pg['lr'] = HP.actor_lr
        print(f"[Train] Actor lr patched → {HP.actor_lr}")
        # Reset log_alpha to alpha_min (checkpoint may have saved α=2.0 from explosion)
        import math as _math
        agent.log_alpha.data.fill_(_math.log(HP.alpha_min))
        print(f"[Train] log_alpha reset → α={HP.alpha_min}")
        # Reset _step so warmup exploration fires again → diverse buffer
        # The critic was trained on fixed-policy actions only; actor needs diverse
        # (s,a) pairs so the critic's Q-gradient is reliable before actor updates.
        agent._step = 0
        print(f"[Train] _step reset → 0  (warmup will refill buffer with random actions)")

    best_reward = -float("inf")

    # ── Episode loop ──────────────────────────────────────────────────
    for episode in range(1, n_episodes + 1):
        obs, _ = env.reset()

        ep_reward    = 0.0
        ep_overloads = 0
        ep_max_sev   = 0.0
        ep_avg_util  = 0.0
        ep_shed      = 0.0
        ep_c_losses : List[float] = []
        ep_a_losses : List[float] = []
        done = False

        while not done:
            # ── Action selection ──────────────────────────────────────
            if eval_only:
                action = agent.select_action(obs, deterministic=True)
            elif agent._step < HP.warmup_steps:
                action = agent.random_action()
            else:
                action = agent.select_action(obs, deterministic=False)

            next_obs, reward, terminated, truncated, info = env.step(action)
            done = terminated or truncated

            # ── Store + learn ─────────────────────────────────────────
            if not eval_only:
                agent.push(obs, action, reward, next_obs, done)
                c_loss, a_loss, _ = agent.learn()
                if c_loss is not None:
                    ep_c_losses.append(c_loss)
                    ep_a_losses.append(a_loss)

            # ── Accumulate episode stats ──────────────────────────────
            ep_reward    += reward
            ep_overloads += info.get("n_overloaded", 0)
            ep_max_sev    = max(ep_max_sev, info.get("max_severity",   0.0))
            ep_avg_util  += info.get("avg_util",      0.0)
            ep_shed      += info.get("power_shed_est", 0.0)

            # ── Monthly progress print ────────────────────────────────
            csv_row = info.get("csv_row", 0)
            if csv_row > 0 and csv_row % 720 == 0:
                month = csv_row // 720
                warmup_tag = " [WARMUP]" if agent._step < HP.warmup_steps else ""
                print(
                    f"  [Ep {episode}] Month {month:2d}/12  "
                    f"row {csv_row}/8759  "
                    f"R={ep_reward:.1f}  "
                    f"α={agent.alpha:.4f}"
                    f"{warmup_tag}"
                )

            obs = next_obs

        # ── End-of-episode bookkeeping ────────────────────────────────
        steps      = env._step_count
        avg_util   = ep_avg_util / max(steps, 1)
        c_loss_avg = float(np.mean(ep_c_losses)) if ep_c_losses else 0.0
        a_loss_avg = float(np.mean(ep_a_losses)) if ep_a_losses else 0.0

        if episode % HP.log_freq == 0:
            logger.log(
                episode, ep_reward, steps, ep_overloads, ep_max_sev,
                avg_util, ep_shed, agent.alpha,
                c_loss_avg, a_loss_avg, agent._step,
            )

        if not eval_only:
            if ep_reward > best_reward:
                best_reward = ep_reward
                agent.save(os.path.join(HP.save_dir, "best_sac.pt"))

            if episode % 25 == 0:
                agent.save(
                    os.path.join(HP.save_dir, f"ep{episode:05d}_sac.pt")
                )

    env.close()
    print("[Train] Done.")
    return agent


# ── CLI entry-point ───────────────────────────────────────────────────────────

if __name__ == "__main__":
    p = argparse.ArgumentParser(description="SAC training for Power Grid")
    p.add_argument("--episodes",      type=int,   default=200)
    p.add_argument("--resume",        type=str,   default=None)
    p.add_argument("--eval",          action="store_true")
    p.add_argument("--n_subs",        type=int,   default=HP.n_subs)
    p.add_argument("--hidden_dim",    type=int,   default=HP.hidden_dim)
    p.add_argument("--actor_lr",      type=float, default=HP.actor_lr)
    p.add_argument("--critic_lr",     type=float, default=HP.critic_lr)
    p.add_argument("--batch",         type=int,   default=HP.batch_size)
    p.add_argument("--replay",        type=int,   default=HP.replay_capacity)
    p.add_argument("--warmup",        type=int,   default=HP.warmup_steps)
    p.add_argument("--reward_scale",  type=float, default=HP.reward_scale)
    p.add_argument("--entropy_scale", type=float, default=HP.entropy_scale)
    args = p.parse_args()

    # Patch HParams in place
    HP.n_subs          = args.n_subs
    HP.hidden_dim      = args.hidden_dim
    HP.actor_lr        = args.actor_lr
    HP.critic_lr       = args.critic_lr
    HP.batch_size      = args.batch
    HP.replay_capacity = args.replay
    HP.warmup_steps    = args.warmup
    HP.reward_scale    = args.reward_scale
    HP.entropy_scale   = args.entropy_scale

    train(n_episodes=args.episodes, resume=args.resume, eval_only=args.eval)