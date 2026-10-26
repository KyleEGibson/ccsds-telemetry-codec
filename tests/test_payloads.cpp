#include <gtest/gtest.h>

#include <stdexcept>

#include "tlm/payloads.hpp"

namespace {

tlm::Housekeeping nominal() {
    tlm::Housekeeping hk;
    hk.mode = tlm::Mode::Ready;
    hk.detector_temp_c = -40.12;
    hk.bus_voltage_v = 28.013;
    hk.current_a = 3.42;
    hk.azimuth_deg = -169.999;
    hk.elevation_deg = 89.5;
    hk.dead_pixels = 2;
    hk.cooler_ready = true;
    hk.gimbal_moving = false;
    hk.bit_fault = true;
    return hk;
}

}  // namespace

TEST(Housekeeping, RoundTripWithinQuantization) {
    const auto bytes = tlm::pack(nominal());
    ASSERT_EQ(bytes.size(), tlm::kHousekeepingSize);
    const auto result = tlm::unpack_housekeeping(bytes);
    ASSERT_TRUE(result.ok());
    const auto& hk = result.value();
    EXPECT_EQ(hk.mode, tlm::Mode::Ready);
    EXPECT_NEAR(hk.detector_temp_c, -40.12, 0.005);
    EXPECT_NEAR(hk.bus_voltage_v, 28.013, 0.0005);
    EXPECT_NEAR(hk.current_a, 3.42, 0.0005);
    EXPECT_NEAR(hk.azimuth_deg, -169.999, 0.0005);
    EXPECT_NEAR(hk.elevation_deg, 89.5, 0.0005);
    EXPECT_EQ(hk.dead_pixels, 2);
    EXPECT_TRUE(hk.cooler_ready);
    EXPECT_FALSE(hk.gimbal_moving);
    EXPECT_TRUE(hk.bit_fault);
}

TEST(Housekeeping, KnownWireEncoding) {
    tlm::Housekeeping hk;
    hk.mode = tlm::Mode::Safe;
    hk.detector_temp_c = -1.0;   // -100 -> 0xFF9C
    hk.bus_voltage_v = 28.0;     // 28000 -> 0x6D60
    const auto bytes = tlm::pack(hk);
    EXPECT_EQ(bytes[0], 0x00);
    EXPECT_EQ(bytes[1], 0x03);
    EXPECT_EQ(bytes[2], 0xFF);
    EXPECT_EQ(bytes[3], 0x9C);
    EXPECT_EQ(bytes[4], 0x6D);
    EXPECT_EQ(bytes[5], 0x60);
}

TEST(Housekeeping, UnrepresentableValuesThrow) {
    auto hk = nominal();
    hk.detector_temp_c = 400.0;  // 40000 overflows int16
    EXPECT_THROW(tlm::pack(hk), std::out_of_range);
    hk = nominal();
    hk.bus_voltage_v = -1.0;
    EXPECT_THROW(tlm::pack(hk), std::out_of_range);
}

TEST(Housekeeping, RejectsWrongSizeAndBadFields) {
    auto bytes = tlm::pack(nominal());
    bytes.pop_back();
    EXPECT_EQ(tlm::unpack_housekeeping(bytes).error(), tlm::DecodeError::BadPayloadSize);

    bytes = tlm::pack(nominal());
    bytes[1] = 9;  // mode 9 does not exist
    EXPECT_EQ(tlm::unpack_housekeeping(bytes).error(), tlm::DecodeError::BadPayloadField);

    bytes = tlm::pack(nominal());
    bytes[18] |= 0x80;  // undefined status flag
    EXPECT_EQ(tlm::unpack_housekeeping(bytes).error(), tlm::DecodeError::BadPayloadField);
}

TEST(Event, RoundTripAndLimits) {
    const tlm::Event event{2, "Cooler setpoint not reached"};
    const auto result = tlm::unpack_event(tlm::pack(event));
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.value().severity, 2);
    EXPECT_EQ(result.value().text, event.text);

    EXPECT_THROW(tlm::pack(tlm::Event{4, "x"}), std::out_of_range);
    EXPECT_THROW(tlm::pack(tlm::Event{0, std::string(256, 'x')}), std::out_of_range);
    EXPECT_TRUE(tlm::unpack_event(tlm::pack(tlm::Event{0, std::string(255, 'x')})).ok());
}

TEST(Event, RejectsBadEncodings) {
    auto bytes = tlm::pack(tlm::Event{1, "hello"});
    bytes[1] = 9;  // length says 9, only 5 present
    EXPECT_EQ(tlm::unpack_event(bytes).error(), tlm::DecodeError::BadPayloadSize);
    bytes = tlm::pack(tlm::Event{1, "hello"});
    bytes[3] = 0x07;  // control character
    EXPECT_EQ(tlm::unpack_event(bytes).error(), tlm::DecodeError::BadPayloadField);
}
