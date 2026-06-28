#include <gtest/gtest.h>
#include "db.hpp"
#include <memory>

using namespace billminder;

class DatabaseTest : public ::testing::Test {
protected:
    void SetUp() override {
        db = std::make_unique<Database>(":memory:");
        db->initialize_schema();
    }

    void TearDown() override {
        db.reset();
    }

    std::unique_ptr<Database> db;
};

TEST_F(DatabaseTest, CreateAndRetrieveBillAndInstance) {
    Bill b;
    b.id = "bill-123";
    b.name = "Internet";
    b.default_amount = 50.0;
    b.recurrence_rule = "monthly";
    b.payee = "Comcast";
    b.notes = "Test bill";
    b.created_at = "2026-06-24";
    b.updated_at = "2026-06-24";

    db->create_bill(b);

    BillInstance inst;
    inst.id = "inst-1";
    inst.bill_id = "bill-123";
    inst.due_date = "2026-07-01";
    inst.amount_expected = 50.0;
    inst.status = "upcoming";
    inst.notes = "Test inst";
    inst.created_at = "2026-06-24";
    inst.updated_at = "2026-06-24";

    db->create_instance(inst);

    auto bills = db->get_bills();
    ASSERT_EQ(bills.size(), 1);
    EXPECT_EQ(bills[0].id, "bill-123");
    EXPECT_DOUBLE_EQ(bills[0].default_amount, 50.0);

    auto instances = db->get_instances();
    ASSERT_EQ(instances.size(), 1);
    EXPECT_EQ(instances[0].bill_name, "Internet");

    Bill retrieved = db->get_bill("bill-123");
    EXPECT_EQ(retrieved.name, "Internet");

    BillInstance retrieved_inst = db->get_instance("inst-1");
    EXPECT_EQ(retrieved_inst.status, "upcoming");
}

TEST_F(DatabaseTest, BillMetadataAndEncryption) {
    Bill b;
    b.id = "bill-metadata-1";
    b.name = "Secret Service";
    b.default_amount = 9.99;
    b.recurrence_rule = "monthly";
    b.url = "https://secretservice.example.com";
    b.account = "agent007";
    b.password = "supersecretpassword!";
    b.created_at = "2026-06-25";
    b.updated_at = "2026-06-25";

    db->create_bill(b);

    Bill retrieved = db->get_bill("bill-metadata-1");
    EXPECT_EQ(retrieved.url, "https://secretservice.example.com");
    EXPECT_EQ(retrieved.account, "agent007");
    EXPECT_EQ(retrieved.password, "supersecretpassword!");
}

TEST_F(DatabaseTest, UpdateInstance) {
    Bill b;
    b.id = "bill-456";
    b.name = "Water";
    b.default_amount = 30.0;
    b.recurrence_rule = "none";
    b.created_at = "2026-06-24";
    b.updated_at = "2026-06-24";
    db->create_bill(b);

    BillInstance inst;
    inst.id = "inst-456";
    inst.bill_id = "bill-456";
    inst.due_date = "2026-07-05";
    inst.amount_expected = 30.0;
    inst.status = "upcoming";
    inst.created_at = "2026-06-24";
    inst.updated_at = "2026-06-24";
    db->create_instance(inst);

    inst.amount_expected = 35.0;
    inst.status = "overdue";
    db->update_instance(inst);

    BillInstance retrieved = db->get_instance("inst-456");
    EXPECT_DOUBLE_EQ(retrieved.amount_expected, 35.0);
    EXPECT_EQ(retrieved.status, "overdue");
}

TEST_F(DatabaseTest, PayInstance) {
    Bill b;
    b.id = "bill-789";
    b.name = "Electricity";
    b.default_amount = 100.0;
    b.recurrence_rule = "monthly";
    b.created_at = "2026-06-24";
    b.updated_at = "2026-06-24";
    db->create_bill(b);

    BillInstance inst;
    inst.id = "inst-789";
    inst.bill_id = "bill-789";
    inst.due_date = "2026-07-10";
    inst.amount_expected = 100.0;
    inst.status = "upcoming";
    inst.created_at = "2026-06-24";
    inst.updated_at = "2026-06-24";
    db->create_instance(inst);

    db->pay_instance("inst-789", 105.0, "2026-07-09", "Paid slightly more due to extra usage");

    BillInstance retrieved = db->get_instance("inst-789");
    EXPECT_EQ(retrieved.status, "paid");
    EXPECT_EQ(retrieved.updated_at, "2026-07-09");
}
