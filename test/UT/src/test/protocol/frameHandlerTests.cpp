#include "protocol/frameHandler.hpp"

#include <gtest/gtest.h>

#include "serializer/serializer.hpp"

#include "matcher/arrayCompare.hpp"
#include "stub/receiverStub.hpp"

namespace protocol
{

const auto emptyFrameReceiver = [](const IFrame& frame) {};

class FrameHandlerShould : public ::testing::Test
{
public:
    FrameHandlerShould() : receiver_(new stub::ReceiverStub())
    {
    }

    void SetUp() override
    {
        handler_.setConnection(receiver_);
    }

protected:
    FrameHandler handler_;
    std::shared_ptr<stub::ReceiverStub> receiver_;
};

TEST_F(FrameHandlerShould, Success)
{
    const u8 payload[] = {0x12, 0xaa};
    u8 frameNumber = 0;
    const u16 testingPort = 10;
    const u16 controlByte = messages::Control::Transmission;

    u8 crc[2];
    serializer::serialize(crc, CRC::Calculate(payload, sizeof(payload), CRC::CRC_16_ARC()));

    const u8 frame[] = {FrameByte::Start, sizeof(payload), frameNumber, testingPort,
                        controlByte,      payload[0],      payload[1],  crc[0],
                        crc[1],           FrameByte::End};

    handler_.connect(testingPort, emptyFrameReceiver);
    receiver_->readerCallback(frame , defaultWriter);

    const u8 expectedNackFrame[] = {FrameByte::Start,           0,   frameNumber, testingPort,
                                    messages::Control::Success, 0x0, 0x0,         FrameByte::End};

    EXPECT_THAT(receiver_->writeBuffer.data(),
                ArrayCompare(expectedNackFrame, sizeof(expectedNackFrame)));

    receiver_->writeBuffer.clear();
    receiver_->readerCallback(frame, defaultWriter);

    EXPECT_THAT(receiver_->writeBuffer.data(),
                ArrayCompare(expectedNackFrame, sizeof(expectedNackFrame)));
}

TEST_F(FrameHandlerShould, NackWithPortNotConnected)
{
    const u8 payload[] = {0x12, 0xaa};
    u8 frameNumber = 0;
    const u16 testingPort = 10;
    const u16 controlByte = messages::Control::Transmission;

    const u8 frame[] = {
        FrameByte::Start, sizeof(payload), frameNumber, testingPort, controlByte,
        payload[0],       payload[1],      0x0,         0x0,         FrameByte::End};

    receiver_->readerCallback(frame, defaultWriter);

    const u8 expectedNackFrame[] = {
        FrameByte::Start, 0, frameNumber, testingPort, messages::Control::PortNotConnect, 0x0, 0x0,
        FrameByte::End};

    EXPECT_THAT(receiver_->writeBuffer.data(),
                ArrayCompare(expectedNackFrame, sizeof(expectedNackFrame)));
}

TEST_F(FrameHandlerShould, NackWhenCrcCalculationFailed)
{
    const u8 payload[] = {0x12, 0xaa};
    u8 frameNumber = 0;
    const u16 testingPort = 10;
    const u16 controlByte = messages::Control::Transmission;

    const u8 frame[] = {
        FrameByte::Start, sizeof(payload), frameNumber, testingPort, controlByte,
        payload[0],       payload[1],      0x0,         0x0,         FrameByte::End};

    handler_.connect(testingPort, emptyFrameReceiver);
    receiver_->readerCallback(frame, defaultWriter);

    const u8 expectedNackFrame[] = {FrameByte::Start,
                                    0,
                                    frameNumber,
                                    testingPort,
                                    messages::Control::CrcChecksumFailed,
                                    0x0,
                                    0x0,
                                    FrameByte::End};

    EXPECT_THAT(receiver_->writeBuffer.data(),
                ArrayCompare(expectedNackFrame, sizeof(expectedNackFrame)));
}

TEST_F(FrameHandlerShould, NackWhenWrongEndByteReceived)
{
    const u8 payload[] = {0x12, 0xaa};
    u8 frameNumber = 0;
    const u16 testingPort = 10;
    const u16 controlByte = messages::Control::Transmission;

    u8 crc[2];
    serializer::serialize(crc, CRC::Calculate(payload, sizeof(payload), CRC::CRC_16_ARC()));

    const u8 frame[] = {FrameByte::Start, sizeof(payload), frameNumber, testingPort,
                        controlByte,      payload[0],      payload[1],  crc[0],
                        crc[1],           FrameByte::Start};

    handler_.connect(testingPort, emptyFrameReceiver);
    receiver_->readerCallback(frame, defaultWriter);

    const u8 expectedNackFrame[] = {
        FrameByte::Start, 0, frameNumber, testingPort, messages::Control::WrongEndByte, 0x0, 0x0,
        FrameByte::End};

    EXPECT_THAT(receiver_->writeBuffer.data(),
                ArrayCompare(expectedNackFrame, sizeof(expectedNackFrame)));
}

TEST_F(FrameHandlerShould, SuccessWithEmptyPayload)
{
    u8 frameNumber = 0;
    const u16 testingPort = 10;
    const u16 controlByte = messages::Control::Transmission;

    u8 crc[2] = {0, 0};

    const u8 frame[] = {FrameByte::Start, 0,      frameNumber, testingPort,
                        controlByte,      crc[0], crc[1],      FrameByte::End};

    handler_.connect(testingPort, emptyFrameReceiver);
    receiver_->readerCallback(frame, defaultWriter);

    const u8 expectedAckFrame[] = {FrameByte::Start,           0,   frameNumber, testingPort,
                                   messages::Control::Success, 0x0, 0x0,         FrameByte::End};

    EXPECT_THAT(receiver_->writeBuffer.data(),
                ArrayCompare(expectedAckFrame, sizeof(expectedAckFrame)));
}
}


// TODO: Test na ramkowanie 0 payload