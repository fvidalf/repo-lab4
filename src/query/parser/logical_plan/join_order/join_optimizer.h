#pragma once

#include <cassert>
#include <limits>
#include <memory>
#include <unordered_map>
#include <vector>

#include "query/parser/logical_plan/join_order/join_cost_estimator.h"
#include "query/parser/logical_plan/logical_plan.h"
#include "query/parser/logical_plan/relation_plan.h"

class JoinOptimizer {
public:
  static std::vector<std::unique_ptr<LogicalPlan>> greedy(
      std::vector<std::unique_ptr<LogicalPlan>>&& relations,
      const std::vector<std::pair<Column, Column>>& join_columns
  ) {

    assert(relations.size() > 1);

    auto relation_estimators = setup_relation_estimators(relations, join_columns);

    // select first 2 relations
    int best_lhs_index = 0;
    int best_rhs_index = 1;
    double best_cost = std::numeric_limits<double>::infinity();

    // TODO: set best_lhs_index and best_rhs_index to choose best join of 2 relations
    for (size_t i = 0; i < relation_estimators.size(); ++i) {
      for (size_t j = i + 1; j < relation_estimators.size(); ++j) {
        auto temp_estimator = std::make_unique<JoinEstimator>(
            relation_estimators[i]->clone(), 
            relation_estimators[j]->clone(),
            join_columns
        );
        double current_cost = temp_estimator->estimate_cost();

        if (current_cost < best_cost) {
          best_cost = current_cost;
          best_lhs_index = i;
          best_rhs_index = j;
        }
      }
    }

    auto current_estimator = std::make_unique<JoinEstimator>(
        std::move(relation_estimators[best_lhs_index]), 
        std::move(relation_estimators[best_rhs_index]),
        join_columns
    );

    // TODO: implement greedy iteration
    while (true) {
      int best_next_index = -1;
      double best_next_cost = std::numeric_limits<double>::infinity();

      for (size_t i = 0; i < relation_estimators.size(); ++i) {
        // If this relation has already been used, skip 
        if (!relation_estimators[i]) continue;

        // Try out this new combination
        auto temp_estimator = std::make_unique<JoinEstimator>(
          current_estimator->clone(),
          relation_estimators[i]->clone(),
          join_columns
        );

        double current_cost = temp_estimator->estimate_cost();

        if (current_cost < best_next_cost) {
          best_next_cost = current_cost;
          best_next_index = i;
        }
      }

      // All relations used, we can stop looping
      if (best_next_index == -1) break;

      current_estimator = std::make_unique<JoinEstimator>(
        std::move(current_estimator),
        std::move(relation_estimators[best_next_index]),
        join_columns
      );
    }

    auto res = current_estimator->get_join_order();

    assert(res.size() == relations.size());
    return res;
  }

  static std::vector<std::unique_ptr<LogicalPlan>> selinger(
      std::vector<std::unique_ptr<LogicalPlan>>&& relations,
      const std::vector<std::pair<Column, Column>>& join_columns
  ) {
    // Ensure we have at least 2 relations
    assert(relations.size() > 1);

    auto relation_estimators = setup_relation_estimators(relations, join_columns);

    // Structure to store join estimator along with its cost
    struct OptimalPlanInfo {
      std::unique_ptr<JoinEstimator> estimator;
      double cost;
        
      OptimalPlanInfo() : cost(std::numeric_limits<double>::infinity()) {}
      OptimalPlanInfo(std::unique_ptr<JoinEstimator> est, double c) 
        : estimator(std::move(est)), cost(c) {}
    };

    // Map to store optimal plan for each subset of relations
    std::unordered_map<SubsetID, OptimalPlanInfo> optimal_plans;

    // Initialize with 2-relation joins as base case
    for (size_t i = 0; i < relation_estimators.size(); ++i) {
      for (size_t j = i + 1; j < relation_estimators.size(); ++j) {
        SubsetID pair_subset = (1ULL << i) | (1ULL << j);
        
        // Try both (R1 JOIN R2) and (R2 JOIN R1)
        auto estimator1 = std::make_unique<JoinEstimator>(
          relation_estimators[i]->clone(),
          relation_estimators[j]->clone(),
          join_columns
        );
        double cost1 = estimator1->estimate_cost();
        
        auto estimator2 = std::make_unique<JoinEstimator>(
          relation_estimators[j]->clone(),
          relation_estimators[i]->clone(),
          join_columns
        );
        double cost2 = estimator2->estimate_cost();
        
        if (cost1 <= cost2) {
          optimal_plans[pair_subset] = OptimalPlanInfo(std::move(estimator1), cost1);
        } else {
          optimal_plans[pair_subset] = OptimalPlanInfo(std::move(estimator2), cost2);
        }
      }
    }

    size_t n = relation_estimators.size();

    for (size_t size = 3; size <= n; size++) {
      auto subsets = generateSubsetsOfSize(n, size);
      
      for (SubsetID S : subsets) {
        double best_cost = std::numeric_limits<double>::infinity();
        std::unique_ptr<JoinEstimator> best_plan = nullptr;
        
        // Pick out single relation r from subset S to attempt JOIN with the rest
        for (size_t r = 0; r < n; r++) {
          // Check if relation r is in subset S
          SubsetID r_bit = (1ULL << r);
          if (!(S & r_bit)) continue;
          
          // S1 = S - {r}
          SubsetID S1 = S & ~r_bit;
          
          // Estimate optimal_plan[S1] JOIN relation[r]
          auto temp_plan = std::make_unique<JoinEstimator>(
            optimal_plans[S1].estimator->clone(),
            relation_estimators[r]->clone(),
            join_columns
          );
          
          double cost = temp_plan->estimate_cost();
          
          // Update if this is better than our current best
          if (cost < best_cost) {
            best_cost = cost;
            best_plan = std::move(temp_plan);
          }
        }
        
        // Store the best plan for subset S
        if (best_plan) {
          optimal_plans[S] = OptimalPlanInfo(std::move(best_plan), best_cost);
        }
      }
    }

    // Return the optimal join order for all relations
    SubsetID all_relations = (1ULL << n) - 1;
    auto res = optimal_plans[all_relations].estimator->get_join_order();
    
    assert(res.size() == relations.size());
    return res;
  }

private:
  using SubsetID = size_t;

  static std::vector<std::unique_ptr<RelationEstimator>> setup_relation_estimators(
      const std::vector<std::unique_ptr<LogicalPlan>>& relations,
      const std::vector<std::pair<Column, Column>>& join_columns) {
    
    std::vector<std::unique_ptr<RelationEstimator>> relation_estimators;

    for (auto& r : relations) {
      const auto relation_plan = dynamic_cast<RelationPlan*>(r.get());
      assert(relation_plan != nullptr);

      std::set<Column> relation_join_cols;

      for (auto&& [col1, col2] : join_columns) {
        if (col1.table == relation_plan->table) {
          relation_join_cols.insert(col1);
        }
        if (col2.table == relation_plan->table) {
          relation_join_cols.insert(col2);
        }
      }
      relation_estimators.push_back(std::make_unique<RelationEstimator>(relation_plan, relation_join_cols));
    }
    
    return relation_estimators;
  }

  // Utility function to generate all subsets of size 'k' from 'n' relations
  static std::vector<SubsetID> generateSubsetsOfSize(size_t n, size_t k) {
    std::vector<SubsetID> result;
    
    // Start with the smallest subset of size k
    SubsetID subset = (1ULL << k) - 1;
    
    // Maximum possible subset value
    SubsetID max_subset = 1ULL << n;
    
    while (subset < max_subset) {
      result.push_back(subset);
      
      // Gosper's hack for next combination
      SubsetID c = subset & -subset;
      SubsetID r = subset + c;
      subset = (((r ^ subset) >> 2) / c) | r;
    }
    
    return result;
  }
};
