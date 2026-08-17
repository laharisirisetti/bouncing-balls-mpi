// Deterministic benchmark input generator.
// Usage: gen_input N M K T [seed]
// Emits a valid bouncing-balls input (uniform random positions/directions) to stdout.
#include <bits/stdc++.h>
using namespace std;

int main(int argc, char **argv){
  if(argc < 5){
    fprintf(stderr, "usage: %s N M K T [seed]\n", argv[0]);
    return 1;
  }
  int N = atoi(argv[1]);
  int M = atoi(argv[2]);
  long long K = atoll(argv[3]);
  int T = atoi(argv[4]);
  unsigned seed = (argc > 5) ? (unsigned)stoul(argv[5]) : 42u;

  mt19937 rng(seed);
  uniform_int_distribution<int> rx(0, N-1), ry(0, M-1), rd(0, 3);
  const char dirs[4] = {'U','D','L','R'};

  string buf;
  buf.reserve((size_t)K * 8 + 32);
  buf += to_string(N); buf += ' ';
  buf += to_string(M); buf += ' ';
  buf += to_string(K); buf += ' ';
  buf += to_string(T); buf += '\n';
  for(long long i=0;i<K;i++){
    buf += to_string(rx(rng)); buf += ' ';
    buf += to_string(ry(rng)); buf += ' ';
    buf += dirs[rd(rng)];      buf += '\n';
  }
  fwrite(buf.data(), 1, buf.size(), stdout);
  return 0;
}
