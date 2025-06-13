#pragma once

#include <cassert>
#include <limits>
#include <memory>
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
    // TODO: bonus:
    return std::vector<std::unique_ptr<LogicalPlan>>();
  }
};
