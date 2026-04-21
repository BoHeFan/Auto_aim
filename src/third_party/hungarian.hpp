#pragma once
#include <vector>
#include <limits>
#include <cmath>
#include <algorithm>

// Proper Munkres (Hungarian) algorithm - O(n^3) optimal assignment
// Replaces the greedy approximation with mathematically correct minimum-cost matching
class HungarianAlgorithm {
public:
    HungarianAlgorithm() {}
    ~HungarianAlgorithm() {}

    // Solve the assignment problem for a cost matrix.
    // Returns assignment vector where assignment[i] = j means row i is assigned to column j.
    // assignment[i] = -1 means row i is unassigned.
    double Solve(std::vector<std::vector<double>>& DistMatrix, std::vector<int>& Assignment) {
        Assignment.clear();
        int nRows = static_cast<int>(DistMatrix.size());
        if (nRows == 0) return 0.0;
        int nCols = static_cast<int>(DistMatrix[0].size());
        if (nCols == 0) {
            Assignment.assign(nRows, -1);
            return 0.0;
        }

        // Pad to square matrix with large costs
        int N = std::max(nRows, nCols);
        const double INF_COST = 1e9;
        std::vector<std::vector<double>> cost(N, std::vector<double>(N, INF_COST));
        for (int i = 0; i < nRows; ++i)
            for (int j = 0; j < nCols; ++j)
                cost[i][j] = DistMatrix[i][j];

        // Hungarian algorithm using potential method (shortest augmenting path)
        std::vector<double> u(N + 1, 0.0), v(N + 1, 0.0);
        std::vector<int> p(N + 1, 0), way(N + 1, 0);

        for (int i = 1; i <= N; ++i) {
            p[0] = i;
            int j0 = 0;
            std::vector<double> minv(N + 1, INF_COST);
            std::vector<bool> used(N + 1, false);

            do {
                used[j0] = true;
                int i0 = p[j0], j1 = 0;
                double delta = INF_COST;
                for (int j = 1; j <= N; ++j) {
                    if (!used[j]) {
                        double cur = cost[i0 - 1][j - 1] - u[i0] - v[j];
                        if (cur < minv[j]) {
                            minv[j] = cur;
                            way[j] = j0;
                        }
                        if (minv[j] < delta) {
                            delta = minv[j];
                            j1 = j;
                        }
                    }
                }
                for (int j = 0; j <= N; ++j) {
                    if (used[j]) {
                        u[p[j]] += delta;
                        v[j] -= delta;
                    } else {
                        minv[j] -= delta;
                    }
                }
                j0 = j1;
            } while (p[j0] != 0);

            // Update matching along the augmenting path
            do {
                int j1 = way[j0];
                p[j0] = p[j1];
                j0 = j1;
            } while (j0);
        }

        // Build assignment result
        Assignment.assign(nRows, -1);
        double totalCost = 0.0;
        for (int j = 1; j <= N; ++j) {
            if (p[j] != 0) {
                int row = p[j] - 1;
                int col = j - 1;
                if (row < nRows && col < nCols) {
                    Assignment[row] = col;
                    totalCost += DistMatrix[row][col];
                }
            }
        }
        return totalCost;
    }
};