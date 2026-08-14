#include <bits/stdc++.h>
using namespace std;

struct Ball{
  char direction;
  long long pos; // flattened: x * M + y
};

long long flatten(int x, int y, int M){
  return (long long)x * M + y;
}

pair<int,int> decode(long long pos, int M){
  int x = pos / M;
  int y = pos % M;
  return {x,y};
}

long long calculateNextPos(Ball ball, int N, int M){
  auto [x,y] = decode(ball.pos, M);
  if(ball.direction == 'U'){
    x = (x - 1 + N) % N;
  }
  else if(ball.direction == 'D'){
    x = (x + 1 + N) % N;
  }
  else if(ball.direction == 'L'){
    y = (y - 1 + M) % M;
  }
  else if(ball.direction == 'R'){
    y = (y + 1 + M) % M;
  }
  return flatten(x,y,M);
}

char nextDirection(char currDirection, int freq){
  if(freq == 1 || freq == 3){
    return currDirection;
  }

  if(freq == 2){
    if(currDirection == 'U') return 'R';
    else if(currDirection == 'R') return 'D';
    else if(currDirection == 'D') return 'L';
    else if(currDirection == 'L') return 'U';
  }

  if(freq == 4){
    if(currDirection == 'U') return 'D';
    else if(currDirection == 'R') return 'L';
    else if(currDirection == 'D') return 'U';
    else if(currDirection == 'L') return 'R';
  }

  return currDirection;
}

char calculateNextDirection(Ball ball, unordered_map<long long, int> &pos_freq){
  int freq = pos_freq[ball.pos];
  return nextDirection(ball.direction, freq);
}

int main(){
  // read input
  int N,M,K,T;
  cin>>N>>M>>K>>T;

  vector<Ball> balls(K);
  for(int i=0;i<K;i++){
    int x,y;
    char d;
    cin>>x>>y>>d;
    balls[i].direction = d;
    balls[i].pos = flatten(x,y,M);
  }

  // process all timesteps
  for(int t=1;t<=T;t++){
    unordered_map<long long, int> pos_freq;
    for(Ball &ball:balls){
      ball.pos = calculateNextPos(ball,N,M);
      pos_freq[ball.pos]++;
    }
    for(Ball &ball:balls){
      ball.direction = calculateNextDirection(ball,pos_freq);
    }
  }

  // print balls final pos and direction
  for(Ball ball:balls){
    auto [x,y] = decode(ball.pos, M);
    cout<<x<<" "<<y<<" "<<ball.direction<<endl;
  }
}