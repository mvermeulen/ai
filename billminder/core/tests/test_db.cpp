#include <gtest/gtest.h>
#include "db.hpp"
#include <memory>

using namespace billminder;

class DatabaseTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Use an in-memory database for testing
        db = std::make_unique<Database>(":memory:");
        db->initialize_schema();
    }

    void TearDown() override {
        db.reset();
    }

    std::unique_ptr<Database> db;
};

TEST_F(DatabaseTest, CreateAndRetrieveBill) {
    Bill b;
    b.id = "bill-123";
    b.name = "Internet";
    b.amount_expected = 50.0;
    b.due_date = "2026-07-01";
    b.recurrence_rule = "monthly";
    b.payee = "Comcast";
    b.status = "upcoming";
    b.notes = "Test bill";
    b.created_at = "2026-06-24";
    b.updated_at = "2026-06-24";

    db->create_bill(b);

    auto bills = db->get_bills();
    ASSERT_EQ(bills.size(), 1);
    EXPECT_EQ(bills[0].id, "bill-123");
    EXPECT_EQ(bills[0].name, "Internet");
    EXPECT_DOUBLE_EQ(bills[0].amount_expected, 50.0);

    Bill retrieved = db->get_bill("bill-123");
    EXPECT_EQ(retrieved.name, "Internet");
}

TEST_F(DatabaseTest, UpdateBill) {
    Bill b;
    b.id = "bill-456";
    b.name = "Water";
    b.amount_expected = 30.0;
    b.due_date = "2026-07-05";
    b.recurrence_rule = "none";
    b.payee = "City";
    b.status = "upcoming";
    b.notes = "";
    b.created_at = "2026-06-24";
    b.updated_at = "2026-06-24";

    db->create_bill(b);

    b.amount_expected = 35.0;
    b.status = "overdue";
    db->update_bill(b);

    Bill retrieved = db->get_bill("bill-456");
    EXPECT_DOUBLE_EQ(retrieved.amount_expected, 35.0);
    EXPECT_EQ(retrieved.status, "overdue");
}

TEST_F(DatabaseTest, PayBill) {
    Bill b;
    b.id = "bill-789";
    b.name = "Electricity";
    b.amount_expected = 100.0;
    b.due_date = "2026-07-10";
    b.recurrence_rule = "monthly";
    b.payee = "PowerCo";
    b.status = "upcoming";
    b.notes = "";
    b.created_at = "2026-06-24";
    b.updated_at = "2026-06-24";

    db->create_bill(b);

    db->pay_bill("bill-789", 105.0, "2026-07-09", "Paid slightly more due to extra usage");

    Bill retrieved = db->get_bill("bill-789");
    EXPECT_EQ(retrieved.status, "paid");
    EXPECT_EQ(retrieved.updated_at, "2026-07-09");
}
