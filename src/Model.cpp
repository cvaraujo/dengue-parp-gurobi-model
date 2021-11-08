//
// Created by carlos on 06/07/21.
//

#include "../headers/Model.h"

class cyclecallback: public GRBCallback {

public:
  double lastiter, lastnode;
  int numvars;
  int cuts = 0;
  vector<vector<GRBVar>> x, y;

  Graph *graph;

  cyclecallback(Graph *xgraph, int xnumvars, vector<vector<GRBVar>> xx, vector<vector<GRBVar>> yy){
    lastiter = lastnode = 0;
    numvars = xnumvars;
    x = xx;
    y = yy;
    graph = xgraph;
  }

protected:
  void callback() {
    try {
      if(where == GRB_CB_MIPSOL) {
        return;
        int i, n = graph->getN();
        vector<vector<int>> g = vector<vector<int>>(n+2, vector<int>());

        for(i = 0; i <= n; i++)
          for (auto *arc : graph->arcs[i])
            if(getSolution(x[i][arc->getD()]) > 0.5)
              g[i].push_back(arc->getD());

        int s;
        vector<bool> visited(n+2, false);
        vector<vector<int>> conn = vector<vector<int>>(n+2, vector<int>());

        for(i = n; i >= 0; i--) {
          vector<int> stack;
          stack.push_back(i);

          while(!stack.empty()) {
            s = stack.back();
            stack.pop_back();

            if(!visited[s]) {
              conn[i].push_back(s);
              visited[s] = true;
            }

            for(auto k : g[s])
              if(!visited[k]) stack.push_back(k);
          }
        }

        for(i = 0; i < n; i++) {
          if(conn[i].size() > 1) {
            GRBLinExpr expr = 0;
            for(auto v : conn[i]) {
              if(v > n) continue;
              for(auto *arc : graph->arcs[v]) {
                bool isIn = false;
                for(auto k : conn[i])
                  if(k == arc->getD()) isIn = true;
                if(!isIn) expr += x[v][arc->getD()];
              }
            }

            for(auto v : conn[i]) {
              if(v > n) continue;
              for(auto b : graph->nodes[v].second) {
                if(getSolution(y[v][b]) > 0.5) {
                  addLazy(expr >= y[v][b]);
                }
              }
            }
          }
        }
      }
      else if(where == GRB_CB_MIPNODE) {
        int mipStatus = getIntInfo(GRB_CB_MIPNODE_STATUS);
	double nodecnt = getDoubleInfo(GRB_CB_MIP_NODCNT);
        if(mipStatus == GRB_OPTIMAL) {
          int i, n = graph->getN();
	  double tresh = 0;
	  cuts += 1;
	  if (cuts > 90) tresh = 0.9;
	  else tresh = 0.1 + (cuts / 100); 
	  
          ListDigraph ghu;
          ListDigraph::ArcMap<double> capacity(ghu);
          vector<ListDigraph::Node> setNodes = vector<ListDigraph::Node>(n+2);
          ListDigraph::Node source, target;

          for (i = 0; i < n+2; i++) setNodes[i] = ghu.addNode();

          for(i = 0; i <= n; i++)
            for (auto *arc : graph->arcs[i])
              if(getNodeRel(x[i][arc->getD()]) > 0) {
                ListDigraph::Arc ed = ghu.addArc(setNodes[i], setNodes[arc->getD()]);
                capacity[ed] = double(getNodeRel(x[i][arc->getD()]));
              }

          for(i = 0; i < n; i++) {
            for(auto b : graph->nodes[i].second) {
              if (getNodeRel(y[i][b]) >= tresh) {
                Preflow<ListDigraph, ListDigraph::ArcMap<double>> preflow(ghu, capacity, setNodes[i], setNodes[n+1]);
                preflow.runMinCut();

                double cutValue = preflow.flowValue();
                if (cutValue >= getNodeRel(y[i][b])) continue;

                GRBLinExpr expr = 0;
                int n_arcs = 0, s, t;
                for (ListDigraph::ArcIt a(ghu); a != INVALID; ++a) {
                  s = ghu.id(ghu.source(a)), t = ghu.id(ghu.target(a));
                  if (s == n || t == n+1) continue;
                  expr += x[s][t];
                  n_arcs++;
                }

                if(n_arcs < 1) break;
                addCut(expr >= y[i][b]);
                break;
              }
            }
          }
        }
      }
    } catch(GRBException e) {
      cout << "Error number: " << e.getErrorCode() << endl;
      cout << e.getMessage() << endl;
    } catch (...) {
      cout << "Error during callback" << endl;
    }
  }
};

Model::Model(Graph *graph) {
  if (graph != nullptr) {
    this->graph = graph;
  } else exit(EXIT_FAILURE);
}

void Model::createVariables() {
  int o, d, k, n = graph->getN(), m = graph->getM(), b = graph->getB();
  try {
    env.set("LogFile", "MS_mip.log");
    env.start();

    x = vector<vector<GRBVar>>(n+2, vector<GRBVar>(n+2));
    y = vector<vector<GRBVar>>(n, vector<GRBVar>(b));
    t = vector<vector<GRBVar>>(n+2, vector<GRBVar>(n+2));
    // t = vector<GRBVar>(n+2);

    // variables x
    char name[30];
    for (o = 0; o <= n; o++) {
      for (auto *arc : graph->arcs[o]) {
        d = arc->getD();
        sprintf(name, "x_%d_%d", o, d);
        x[o][d] = model.addVar(0.0, 1.0, 0, GRB_BINARY, name);
      }
    }

    // variables y
    for (int i = 0; i < n; i++) {
      o = graph->nodes[i].first;
      for (auto bl : graph->nodes[i].second) {
        sprintf(name, "y_%d_%d", o, bl);
        y[o][bl] = model.addVar(0.0, 1.0, 0, GRB_BINARY, name);
      }
    }

    // variables t
    for (o = 0; o <= n; o++) {
      for (auto *arc : graph->arcs[o]) {
        d = arc->getD();
        sprintf(name, "t_%d_%d", o, d);
        t[o][d] = model.addVar(0.0, GRB_INFINITY, 0.0, GRB_CONTINUOUS, name);
      }
    }

    model.update();
    cout << "Create variables" << endl;
  } catch (GRBException &ex) {
    cout << ex.getMessage() << endl;
    cout << ex.getErrorCode() << endl;
    exit(EXIT_FAILURE);
  }
}

void Model::initModelExp(float maxTime, float maxInsecticide) {
  cout << "Begin the model creation" << endl;
  objectiveFunction();
  artificialNodes(), flowConservation();
  maxAttending(), attendingPath(), timeConstraint(maxTime);
  inseticideConstraint(maxInsecticide);
  cout << "All done!" << endl;
}

void Model::initModelCompact(float maxTime, float maxInsecticide) {
  cout << "Begin the model creation" << endl;
  objectiveFunction();
  artificialNodes(), flowConservation();
  maxAttending(), attendingPath(), compactTimeConstraint(maxTime);
  inseticideConstraint(maxInsecticide);
  cout << "All done!" << endl;
}

void Model::objectiveFunction() {
  GRBLinExpr objective;
  int i, j, n = graph->getN();

  for(i = 0; i < n; i++) {
    j = graph->nodes[i].first;
    for(auto b : graph->nodes[i].second) {
      objective += (y[j][b] * profitBlock(b));
    }
  }

  for(i = 0; i < n; i++) {
    for(auto *arc : graph->arcs[i]) {
      if (arc->getD() >= n) continue;
      objective -= (x[i][arc->getD()]);
    }
  }

  model.setObjective(objective, GRB_MAXIMIZE);
  model.update();
  cout << "Objective Function" << endl;
}

void Model::artificialNodes() {
  int n = graph->getN();
  GRBLinExpr sink, target;

  for(int i = 0; i < n; i++) {
    sink += x[n][i];
    target += x[i][n+1];
  }
  model.addConstr(sink == 1, "sink_constraint");
  model.addConstr(target == 1, "target_constraint");
  cout << "Artificial nodes" << endl;
}

void Model::flowConservation() {
  int i, j, n = graph->getN();

  for (i = 0; i < n; i++) {
    GRBLinExpr flow_out, flow_in;
    for (auto *arc : graph->arcs[i]) {
      if(arc->getD() >= n) continue;
      flow_out += x[i][arc->getD()];
    }
    for (j = 0; j < n; j++) {
      for (auto *arc : graph->arcs[j]) {
        if (arc->getD() == i and arc->getD() < n) flow_in += x[j][i];
      }
    }
    flow_out += x[i][n+1]; flow_in += x[n][i];
    model.addConstr(flow_in - flow_out == 0, "flow_conservation_" + to_string(i));
  }

  cout << "Flow conservation" << endl;
}

void Model::maxAttending() {
  int bl, b = graph->getB();

  for (bl = 0; bl < b; bl++) {
    GRBLinExpr maxServ;
    for(auto i : graph->nodesPerBlock[bl]) {
      maxServ += y[i][bl];
    }
    model.addConstr(maxServ <= 1, "max_service_block_" + to_string(bl));
  }
  cout << "Attending blocks" << endl;
}

void Model::attendingPath() {
  int j, bl, n = graph->getN(), b = graph->getB();

  for (bl = 0; bl < b; bl++) {
    for(auto i : graph->nodesPerBlock[bl]) {
      GRBLinExpr served;
      for(j = 0; j <= n; j++)
        for(auto *arc : graph->arcs[j])
          if (arc->getD() == i) served += x[j][i];
      model.addConstr(served >= y[i][bl], "att_path_" + to_string(bl) + "_" + to_string(i));
    }
  }
  cout << "Make path to attending block" << endl;
}

void Model::timeConstraint(float maxTime) {
  int i, j, n = graph->getN();
  GRBLinExpr arcTravel, blockTravel;

  for(i = 0; i < n; i++) {
    for (auto *arc : graph->arcs[i]) {
      j = arc->getD();
      arcTravel += x[i][j] * timeArc(arc->getLength(), 350);
    }
    for (auto b : graph->nodes[i].second) {
      blockTravel += y[i][b] * timeBlock(250, b);
    }
  }
  model.addConstr(arcTravel + blockTravel <= maxTime, "max_time");
  cout << "Time constraint"<< endl;
}

void Model::compactTimeConstraint(float maxTime) {
  int b, i, j, k, n = graph->getN();

  for(auto *arc: graph->arcs[n]) model.addConstr(t[n][arc->getD()] == 0);

  for(i = 0; i < n; i++) {
    for(auto *arc : graph->arcs[i]) {
      j = arc->getD();
      if(j == n+1) continue;

      b = arc->getBlock();
      GRBLinExpr time_ij = 0;
      time_ij += t[i][j] + (timeArc(arc->getLength(), 350) * x[i][j]);
      if(b != -1) time_ij += timeBlock(250, b) * y[i][b];

      for(auto *arcl : graph->arcs[j]) {
        k = arcl->getD();
        model.addConstr(time_ij >= t[j][k] - (2 - x[i][j] - x[j][k]) * maxTime);//graph->getMtime());
      }
    }
  }
  for(i = 0; i < n; i++) model.addConstr(t[i][n+1] <= maxTime);
  cout << "Time constraint done!" << endl;
}

void Model::inseticideConstraint(float maxInseticide) {
  int i, j, n = graph->getN();
  GRBLinExpr insConsumed;

  for(i = 0; i < n; i++) {
    for(auto b : graph->nodes[i].second) {
      insConsumed += y[i][b] * inseticideBlock(75, b);
    }
  }
  model.addConstr(insConsumed <= maxInseticide, "max_inseticide");
  cout << "Inseticide constraint" << endl;
}

float Model::timeArc(float distance, float speed) {
  return distance > 0 ? distance/speed : 0;
}

float Model::timeBlock(float speed, int block) {
  float time = 0;
  for (auto *arc : graph->arcsPerBlock[block]) {
    time += timeArc(arc->getLength(), speed);
  }
  return time;
}

float Model::inseticideBlock(float perMeter, int block) {
  float consumed = 0;
  for (auto *arc : graph->arcsPerBlock[block]) {
    consumed += timeArc(arc->getLength(), 250) * perMeter;
  }
  return consumed;
}

float Model::profitBlock(int block) {
  int i, n = graph->getN();
  int total = 0;
  for(auto *arc : graph->arcsPerBlock[block]) total += arc->getCases();
  return total;//(float)graph->arcsPerBlock[block].size();
}

void Model::solveCompact(string timeLimit) {
  try {
    model.set("TimeLimit", timeLimit);
    model.update();
    // model.computeIIS();
    model.set("OutputFlag", "0");
    model.write("model.lp");
    model.optimize();
  } catch (GRBException &ex) {
    cout << ex.getMessage() << endl;
  }
}

void Model::solveExp(string timeLimit) {
  try {
    model.set("TimeLimit", timeLimit);

    // model.set(GRB_DoubleParam_Heuristics, 0.0);
    model.set(GRB_IntParam_LazyConstraints, 1);
    cyclecallback cb = cyclecallback(graph, graph->getN(), x, y);
    model.setCallback(&cb);

    model.update();
    model.set("OutputFlag", "0");
    model.write("model.lp");
    model.optimize();
  } catch (GRBException &ex) {
    cout << ex.getMessage() << endl;
  }
}


void Model::showSolution(string result){
  try {
    ofstream output;
    output.open(result);

    int n = graph->getN(), b = graph->getB(), j;
    output << "Nodes: " << n << endl;
    output << "Arcs: " << graph->getM() << endl;
    output << "Blocks: " << b << endl;
    output << "UB: " << model.get(GRB_DoubleAttr_ObjVal) << endl;
    output << "LB: " << model.get(GRB_DoubleAttr_ObjBound) << endl;

    double timeUsed = 0;
    double insecUsed = 0;
    for(int i = 0; i < n; i++) {
      for (auto *arc : graph->arcs[i]) {
        j = arc->getD();
        if (x[i][j].get(GRB_DoubleAttr_X) > 0.5)
          timeUsed += timeArc(arc->getLength(), 500);
      }
      for (auto b : graph->nodes[i].second) {
        if (y[i][b].get(GRB_DoubleAttr_X) > 0.5) {
          timeUsed += timeBlock(250, b);
          insecUsed += inseticideBlock(75, b);
        }
      }
    }

    output << "Used Time: " << timeUsed  << endl;
    output << "Used Insecticide: " << insecUsed  << endl;

    output << "N. Nodes: " << model.get(GRB_DoubleAttr_NodeCount) << endl;
    output << "Runtime: " << model.get(GRB_DoubleAttr_Runtime) << endl;

    cout << "OF: " << model.get(GRB_DoubleAttr_ObjVal) << endl;

    cout << "X" << endl;
    for(int i = 0; i <= n; i++){
      for(auto *arc : graph->arcs[i]) {
        if(x[i][arc->getD()].get(GRB_DoubleAttr_X) > 0)
          output << "X: " << i << " " << arc->getD() << endl;
      }
    }
    cout << "---------------------------------------------" << endl;
    cout << "Y" << endl;
    for (int i = 0; i < n; i++) {
      int o = graph->nodes[i].first;
      for (auto bl : graph->nodes[i].second) {
        if(y[i][bl].get(GRB_DoubleAttr_X) > 0)
          output << "Y: " << i << " " << bl << endl;
      }
    }
  } catch (GRBException &ex) {
    ofstream output;
    output.open(result);

    int n = graph->getN(), b = graph->getB(), j;
    output << "Nodes: " << n << endl;
    output << "Arcs: " << graph->getM() << endl;
    output << "Blocks: " << b << endl;

    double timeUsed = 0;
    double insecUsed = 0;
    for(int i = 0; i < n; i++) {
      for (auto *arc : graph->arcs[i]) {
        j = arc->getD();
        if (x[i][j].get(GRB_DoubleAttr_X) > 0.5)
          timeUsed += timeArc(arc->getLength(), 500);
      }
      for (auto b : graph->nodes[i].second) {
        if (y[i][b].get(GRB_DoubleAttr_X) > 0.5) {
          timeUsed += timeBlock(250, b);
          insecUsed += inseticideBlock(75, b);
        }
      }
    }

    output << "Used Time: " << timeUsed  << endl;
    output << "Used Insecticide: " << insecUsed  << endl;

    output << "UB: 999999" << endl;
    output << "LB: " << model.get(GRB_DoubleAttr_ObjBound) << endl;
    output << "N. Nodes: " << model.get(GRB_DoubleAttr_NodeCount) << endl;
    output << "Runtime: " << model.get(GRB_DoubleAttr_Runtime) << endl;

    cout << "OF: " << model.get(GRB_DoubleAttr_ObjVal) << endl;

    cout << "X" << endl;
    for(int i = 0; i <= n; i++){
      for(auto *arc : graph->arcs[i]) {
        if(x[i][arc->getD()].get(GRB_DoubleAttr_X) > 0)
          output << "X: " << i << " " << arc->getD() << endl;
      }
    }
    cout << "---------------------------------------------" << endl;
    cout << "Y" << endl;
    for (int i = 0; i < n; i++) {
      int o = graph->nodes[i].first;
      for (auto bl : graph->nodes[i].second) {
        if(y[i][bl].get(GRB_DoubleAttr_X) > 0)
          output << "Y: " << i << " " << bl << endl;
      }
    }
   cout << ex.getMessage() << endl;
  }
}
