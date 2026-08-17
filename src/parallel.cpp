#include <mpi.h>
#include <bits/stdc++.h>
using namespace std;

const int MPI_ROOT_PROCESS = 0;

struct MPI_Context{
  int rank;
  int worldSize;
};

struct Ball{
  int id;
  long long pos; //flatten : x*M + y
  char direction;
};

struct GlobalGridInfo{
  int N; // Number of rows
  int M; // Number of Columns
  int K; // Number of Balls
  int T; // Number of time steps
  char split_axis; // grid split across rows / columns
  vector<Ball> balls; // only root contains data
};

struct LocalGridInfo{
  int start; // row / col start
  int end; // row/ col end
  int k; // Number of balls owned 
  vector<Ball> balls;
};

struct DistributionPlan{
  vector<int> bandInfo;    // 3*P : start, end, count per process (only root fills)
  vector<Ball> sendBalls;  // balls grouped contiguously by owning process
  vector<int> sendCounts;  // P : balls per process
  vector<int> displs;      // P : start offset of each process's group in sendBalls
};

// HELPER FUNCTIONS 

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

// END of Helper Funcitons

// Balls are trivially copyable {int, long long, char}; describe the layout to MPI
// so scatter/gather/neighbour-exchange can move them portably.
MPI_Datatype createBallDatatype(){
  MPI_Datatype ballType;
  int blockLengths[3] = {1, 1, 1};
  MPI_Aint displacements[3] = {
    offsetof(Ball, id),
    offsetof(Ball, pos),
    offsetof(Ball, direction)
  };
  MPI_Datatype types[3] = {MPI_INT, MPI_LONG_LONG, MPI_CHAR};
  MPI_Type_create_struct(3, blockLengths, displacements, types, &ballType);
  MPI_Type_commit(&ballType);
  return ballType;
}

// Send sendBuf to `sendTo` and receive the neighbour's buffer from `recvFrom`.
// Counts are exchanged first so the receiver can size its buffer.
vector<Ball> exchangeBalls(vector<Ball> &sendBuf, int sendTo, int recvFrom, MPI_Comm comm, MPI_Datatype ballType){
  int sendCount = (int)sendBuf.size();
  int recvCount = 0;
  MPI_Sendrecv(&sendCount, 1, MPI_INT, sendTo, 0,
               &recvCount, 1, MPI_INT, recvFrom, 0,
               comm, MPI_STATUS_IGNORE);
  vector<Ball> recvBuf(recvCount);
  MPI_Sendrecv(sendBuf.data(), sendCount, ballType, sendTo, 1,
               recvBuf.data(), recvCount, ballType, recvFrom, 1,
               comm, MPI_STATUS_IGNORE);
  return recvBuf;
}

void processTimesteps(GlobalGridInfo &globalGridInfo, LocalGridInfo &local, MPI_Context &mpi_context, MPI_Comm sim_comm, MPI_Datatype ballType){
  int N = globalGridInfo.N, M = globalGridInfo.M, T = globalGridInfo.T;
  int D = max(N, M);
  int P = mpi_context.worldSize, rank = mpi_context.rank;
  int prev = (rank - 1 + P) % P;
  int next = (rank + 1) % P;

  for(int t = 0; t < T; t++){
    // move each owned ball; a ball leaving the band goes to exactly one neighbour
    vector<Ball> keep, toPrev, toNext;
    for(Ball b : local.balls){
      b.pos = calculateNextPos(b, N, M);
      auto [x, y] = decode(b.pos, M);
      int coord = (globalGridInfo.split_axis == 'R') ? x : y;
      if(coord >= local.start && coord <= local.end) keep.push_back(b);
      else if(coord == (local.start - 1 + D) % D) toPrev.push_back(b);
      else toNext.push_back(b);
    }

    if(P > 1){
      vector<Ball> fromPrev = exchangeBalls(toNext, next, prev, sim_comm, ballType);
      vector<Ball> fromNext = exchangeBalls(toPrev, prev, next, sim_comm, ballType);
      for(Ball &b : fromPrev) keep.push_back(b);
      for(Ball &b : fromNext) keep.push_back(b);
    }
    local.balls = move(keep);

    // every ball on a given cell now lives here, so the count is fully local
    unordered_map<long long, int> pos_freq;
    for(Ball &b : local.balls) pos_freq[b.pos]++;
    for(Ball &b : local.balls) b.direction = calculateNextDirection(b, pos_freq);
  }
}

DistributionPlan splitGrid(GlobalGridInfo &globalGridInfo, MPI_Context &mpi_context){
  int D = max(globalGridInfo.N, globalGridInfo.M);
  int P = mpi_context.worldSize;
  int base = D / P;
  int rem = D % P;
  vector<int> d_offsets(P+1);
  d_offsets[0] = 0;
  int start = 0;
  vector<pair<int,int>> range(P);
  for(int p=0;p<P;p++){
    int end = start + base;
    if(p < rem) end++;
    d_offsets[p+1] = end;
    range[p] = {start, end - 1};
    start = end;
  }

  // bin each ball into the band that owns its coordinate along the split axis
  vector<vector<Ball>> processor_balls(P);
  for(const Ball &ball:globalGridInfo.balls){
    auto [x,y] = decode(ball.pos, globalGridInfo.M);
    int coord = (globalGridInfo.split_axis == 'R') ? x : y;
    int processor = upper_bound(d_offsets.begin(), d_offsets.end(), coord) - d_offsets.begin() - 1;
    processor_balls[processor].push_back(ball);
  }

  // flatten into contiguous buffers for MPI_Scatter / MPI_Scatterv
  DistributionPlan plan;
  plan.bandInfo.resize(3 * P);
  plan.sendCounts.resize(P);
  plan.displs.resize(P);
  int offset = 0;
  for(int p=0;p<P;p++){
    plan.bandInfo[3*p + 0] = range[p].first;
    plan.bandInfo[3*p + 1] = range[p].second;
    plan.bandInfo[3*p + 2] = (int)processor_balls[p].size();
    plan.displs[p] = offset;
    plan.sendCounts[p] = (int)processor_balls[p].size();
    for(const Ball &b : processor_balls[p]) plan.sendBalls.push_back(b);
    offset += (int)processor_balls[p].size();
  }
  return plan;
}

LocalGridInfo distributeBalls(DistributionPlan &plan, MPI_Context &mpi_context, MPI_Comm sim_comm, MPI_Datatype ballType){
  // phase A: each rank learns its band {start, end} and ball count
  int myInfo[3];
  MPI_Scatter(plan.bandInfo.data(), 3, MPI_INT,
              myInfo, 3, MPI_INT,
              MPI_ROOT_PROCESS, sim_comm);

  LocalGridInfo local;
  local.start = myInfo[0];
  local.end   = myInfo[1];
  local.k     = myInfo[2];
  local.balls.resize(local.k);

  // phase B: each rank receives its variable-length group of balls
  MPI_Scatterv(plan.sendBalls.data(), plan.sendCounts.data(), plan.displs.data(), ballType,
               local.balls.data(), local.k, ballType,
               MPI_ROOT_PROCESS, sim_comm);
  return local;
}

vector<Ball> gatherBalls(LocalGridInfo &local, MPI_Context &mpi_context, MPI_Comm sim_comm, MPI_Datatype ballType){
  int P = mpi_context.worldSize, rank = mpi_context.rank;
  int localCount = (int)local.balls.size();

  vector<int> recvCounts, displs;
  if(rank == MPI_ROOT_PROCESS) recvCounts.resize(P);
  MPI_Gather(&localCount, 1, MPI_INT, recvCounts.data(), 1, MPI_INT, MPI_ROOT_PROCESS, sim_comm);

  vector<Ball> allBalls;
  if(rank == MPI_ROOT_PROCESS){
    displs.resize(P);
    int total = 0;
    for(int p=0;p<P;p++){ displs[p] = total; total += recvCounts[p]; }
    allBalls.resize(total);
  }
  MPI_Gatherv(local.balls.data(), localCount, ballType,
              allBalls.data(), recvCounts.data(), displs.data(), ballType,
              MPI_ROOT_PROCESS, sim_comm);
  return allBalls;
}

void printOutput(vector<Ball> &allBalls, GlobalGridInfo &globalGridInfo){
  // balls arrive grouped by band; restore original input order
  sort(allBalls.begin(), allBalls.end(), [](const Ball &a, const Ball &b){ return a.id < b.id; });
  for(const Ball &b : allBalls){
    auto [x, y] = decode(b.pos, globalGridInfo.M);
    cout << x << " " << y << " " << b.direction << "\n";
  }
}

void setupSimulationCommunicator(GlobalGridInfo &globalGridInfo, MPI_Context &mpi_context, MPI_Comm &sim_comm){
  int D = max(globalGridInfo.N, globalGridInfo.M);
  globalGridInfo.split_axis = globalGridInfo.N > globalGridInfo.M ? 'R' : 'C';
  int P_active = min(D, mpi_context.worldSize);
  int color = (mpi_context.rank < P_active) ? 0 : MPI_UNDEFINED;
  MPI_Comm_split(MPI_COMM_WORLD, color, mpi_context.rank, &sim_comm);
  if(sim_comm != MPI_COMM_NULL){
    // refresh rank/size to reflect the active sub-communicator
    MPI_Comm_rank(sim_comm, &mpi_context.rank);
    MPI_Comm_size(sim_comm, &mpi_context.worldSize);
  }
}

void broadcastGlobalGridInfo(GlobalGridInfo &globalGridInfo){
  int header[3] = {globalGridInfo.N, globalGridInfo.M, globalGridInfo.T};
  MPI_Bcast(&header, 3, MPI_INT, MPI_ROOT_PROCESS, MPI_COMM_WORLD);
  globalGridInfo.N = header[0], globalGridInfo.M = header[1], globalGridInfo.T = header[2];
}

void readInput(GlobalGridInfo &globalGridInfo){
  cin>>globalGridInfo.N>>globalGridInfo.M>>globalGridInfo.K>>globalGridInfo.T;
  globalGridInfo.balls.resize(globalGridInfo.K);
  for(int i=0;i<globalGridInfo.K;i++){
    int x,y;
    char d;
    cin>>x>>y>>d;
    globalGridInfo.balls[i].id = i;
    globalGridInfo.balls[i].pos = flatten(x,y,globalGridInfo.M);
    globalGridInfo.balls[i].direction = d;
  }
}

void initializeMPI(int argc, char **argv, MPI_Context &mpi_context){
  MPI_Init(&argc, &argv );
  MPI_Comm_rank(MPI_COMM_WORLD, &mpi_context.rank);
  MPI_Comm_size(MPI_COMM_WORLD, &mpi_context.worldSize);
}

int main(int argc, char **argv){
  MPI_Context mpi_context;
  initializeMPI(argc, argv, mpi_context);
  MPI_Datatype ballType = createBallDatatype();
  GlobalGridInfo globalGridInfo;
  if(mpi_context.rank == MPI_ROOT_PROCESS){
    readInput(globalGridInfo);
  }
  broadcastGlobalGridInfo(globalGridInfo);

  MPI_Comm sim_comm;
  setupSimulationCommunicator(globalGridInfo, mpi_context, sim_comm);
  if(sim_comm == MPI_COMM_NULL){
    // extra ranks (P > grid dimension): nothing to do
    MPI_Type_free(&ballType);
    MPI_Finalize();
    return 0;
  }

  // time the parallel algorithm region only (distribute -> simulate -> gather),
  // excluding stdin read and stdout print
  MPI_Barrier(sim_comm);
  double t0 = MPI_Wtime();

  DistributionPlan plan;
  if(mpi_context.rank == MPI_ROOT_PROCESS){
    plan = splitGrid(globalGridInfo, mpi_context);
  }
  LocalGridInfo localGridInfo = distributeBalls(plan, mpi_context, sim_comm, ballType);
  processTimesteps(globalGridInfo, localGridInfo, mpi_context, sim_comm, ballType);
  vector<Ball> gathered = gatherBalls(localGridInfo, mpi_context, sim_comm, ballType);

  MPI_Barrier(sim_comm);
  double elapsed = MPI_Wtime() - t0, maxElapsed;
  MPI_Reduce(&elapsed, &maxElapsed, 1, MPI_DOUBLE, MPI_MAX, MPI_ROOT_PROCESS, sim_comm);

  if(mpi_context.rank == MPI_ROOT_PROCESS){
    fprintf(stderr, "TIME %.6f\n", maxElapsed);
    printOutput(gathered, globalGridInfo);
  }

  MPI_Type_free(&ballType);
  MPI_Finalize();
}