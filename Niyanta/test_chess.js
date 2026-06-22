const { Chess } = require('chess.js');
const chess = new Chess();
try {
  chess.move({ from: 'e2', to: 'e4' });
  console.log("Move success! FEN:", chess.fen());
} catch(e) {
  console.log("Move failed!", e.message);
}
