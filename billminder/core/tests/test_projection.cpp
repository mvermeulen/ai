#include <gtest/gtest.h>
#include "projection.hpp"

using namespace billminder;

TEST(ProjectionTest, AddTimeToDate) {
    // Monthly
    EXPECT_EQ(ProjectionEngine::add_time_to_date("2026-01-15", "monthly", 1), "2026-02-15");
    EXPECT_EQ(ProjectionEngine::add_time_to_date("2026-12-15", "monthly", 1), "2027-01-15");

    // Quarterly
    EXPECT_EQ(ProjectionEngine::add_time_to_date("2026-01-15", "quarterly", 1), "2026-04-15");
    EXPECT_EQ(ProjectionEngine::add_time_to_date("2026-11-15", "quarterly", 1), "2027-02-15");

    // Yearly
    EXPECT_EQ(ProjectionEngine::add_time_to_date("2026-01-15", "yearly", 1), "2027-01-15");
}

TEST(ProjectionTest, GenerateProjectionsWithinHorizon) {
    std::vector<BillInstance> current_bills;
    
    BillInstance b1;
    b1.id = "bill-1";
    b1.due_date = "2026-06-25";
    b1.recurrence_rule = "monthly";
    b1.status = "upcoming";
    current_bills.push_back(b1);

    auto projections = ProjectionEngine::generate_projections(current_bills, 60, "2026-06-20");
    
    ASSERT_EQ(projections.size(), 1);
    EXPECT_EQ(projections[0].id, "bill-1-proj-1");
    EXPECT_EQ(projections[0].due_date, "2026-07-25");
    EXPECT_EQ(projections[0].status, "projected");
}

TEST(ProjectionTest, GenerateMultipleProjections) {
    std::vector<BillInstance> current_bills;
    
    BillInstance b1;
    b1.id = "bill-1";
    b1.due_date = "2026-06-25";
    b1.recurrence_rule = "monthly";
    b1.status = "upcoming";
    current_bills.push_back(b1);
    
    auto projections = ProjectionEngine::generate_projections(current_bills, 90, "2026-06-20");
    
    ASSERT_EQ(projections.size(), 2);
    EXPECT_EQ(projections[0].due_date, "2026-07-25");
    EXPECT_EQ(projections[1].due_date, "2026-08-25");
}

TEST(ProjectionTest, SkipsNonRecurringBills) {
    std::vector<BillInstance> current_bills;
    
    BillInstance b1;
    b1.id = "bill-1";
    b1.due_date = "2026-06-25";
    b1.recurrence_rule = "none";
    b1.status = "upcoming";
    current_bills.push_back(b1);
    
    auto projections = ProjectionEngine::generate_projections(current_bills, 90, "2026-06-20");
    EXPECT_TRUE(projections.empty());
}
