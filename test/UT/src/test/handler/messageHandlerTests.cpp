#include "handler/MessageHandler.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <ArduinoJson.h>

#include "handler/IDataReceiver.hpp"

#include "matcher/arrayCompare.hpp"
#include "message/messages.hpp"
#include "mock/writerHandlerMock.hpp"
#include "serializer/serializer.hpp"

using namespace testing;

namespace handler
{

class MessageHandlerForTest : public MessageHandler
{
public:
    std::string data_;
    void handleData(const DataBuffer& data)
    {
        for (const auto& byte : data)
        {
            data_ += static_cast<char>(byte);
        }
    }
};

struct ReceiverForTest : public handler::IDataReceiver
{
    void setHandler(ReaderCallback readerCallback) override
    {
        readerCallback_ = readerCallback;
    }

    ReaderCallback readerCallback_;
};

// struct MessageHandler
// {
//     std::string data_;

//     void handle(std::vector<u8> data)
//     {
//         for (auto& byte : data)
//         {
//             data_ += static_cast<char>(byte);
//         }
//     }
// };

struct MessageReceiverShould : public testing::Test
{
    MessageReceiverShould();

    MessageHandlerForTest handler_;
    ReceiverForTest receiver_;

    test::mock::WriterHandlerMock writerMock_;
};

MessageReceiverShould::MessageReceiverShould()
{
    receiver_.setHandler(std::bind(&IFrameHandler::onRead, &handler_, std::placeholders::_1,
                                   std::placeholders::_2, std::placeholders::_3));
}

TEST_F(MessageReceiverShould, InitializeMembers)
{
    EXPECT_FALSE(handler_.transmissionStarted());
}

TEST_F(MessageReceiverShould, StartTransmissionAndSendAck)
{
    EXPECT_FALSE(handler_.transmissionStarted());

    const u8 expectedAnswer[] = {message::TransmissionId::Ack};
    u8 msg[] = {message::TransmissionId::Start};


    EXPECT_CALL(writerMock_, doWrite(ArrayCompare(expectedAnswer, sizeof(expectedAnswer)), 1));

    receiver_.readerCallback_(msg, sizeof(msg),
                              [this](const auto* buf, auto len) { writerMock_.doWrite(buf, len); });

    EXPECT_TRUE(handler_.transmissionStarted());
}

TEST_F(MessageReceiverShould, NackWhenNotStartedAndDataArrived)
{
    EXPECT_FALSE(handler_.transmissionStarted());

    const u8 expectedAnswer[] = {message::TransmissionId::Nack};
    const u8 someMessageDifferentThatStart = ~message::TransmissionId::Start;
    const u8 msg[] = {someMessageDifferentThatStart};

    EXPECT_CALL(writerMock_, doWrite(ArrayCompare(expectedAnswer, sizeof(expectedAnswer)), 1));

    receiver_.readerCallback_(msg, sizeof(msg),
                              [this](const auto* buf, auto len) { writerMock_.doWrite(buf, len); });

    EXPECT_FALSE(handler_.transmissionStarted());
}

TEST_F(MessageReceiverShould, SendMessageLength)
{
    EXPECT_FALSE(handler_.transmissionStarted());

    const u8 expectedAnswer[] = {message::TransmissionId::Nack};
    const u8 someMessageDifferentThatStart = ~message::TransmissionId::Start;
    const u8 msg[] = {someMessageDifferentThatStart};

    EXPECT_CALL(writerMock_, doWrite(ArrayCompare(expectedAnswer, sizeof(expectedAnswer)), 1));

    receiver_.readerCallback_(msg, sizeof(msg),
                              [this](const auto* buf, auto len) { writerMock_.doWrite(buf, len); });

    EXPECT_FALSE(handler_.transmissionStarted());
}

TEST_F(MessageReceiverShould, ReceiveMessageLengthCorrectly)
{
    EXPECT_FALSE(handler_.transmissionStarted());

    const u8 expectedAnswer[] = {message::TransmissionId::Ack};

    const u64 messageLength = 0x0f;

    u8 msg[9] = {message::TransmissionId::Start};
    serializer::serialize(&msg[1], messageLength);

    EXPECT_EQ(0, handler_.lengthToBeReceived());
    EXPECT_CALL(writerMock_, doWrite(ArrayCompare(expectedAnswer, sizeof(expectedAnswer)), 1));

    receiver_.readerCallback_(msg, sizeof(msg),
                              [this](const auto* buf, auto len) { writerMock_.doWrite(buf, len); });

    EXPECT_EQ(messageLength, handler_.lengthToBeReceived());
    EXPECT_TRUE(handler_.transmissionStarted());
}

TEST_F(MessageReceiverShould, ReceiveMessageCorrectly)
{
    EXPECT_FALSE(handler_.transmissionStarted());

    DynamicJsonBuffer messageBuffer;
    JsonObject& message = messageBuffer.createObject();

    message["testing_string"] = "dynamic payload works";
    message["testing_float"] = 3.14;

    JsonArray& testingArray = message.createNestedArray("testing_array");
    testingArray.add(1);
    testingArray.add(2);
    testingArray.add(3);

    std::string serializedMessage;

    message.printTo(serializedMessage);

    u8 msg[9] = {message::TransmissionId::Start};
    serializer::serialize(&msg[1], serializedMessage.length());

    EXPECT_EQ(0, handler_.lengthToBeReceived());
    const u8 expectedAnswer[] = {message::TransmissionId::Ack};
    EXPECT_CALL(writerMock_, doWrite(ArrayCompare(expectedAnswer, sizeof(expectedAnswer)), 1));

    // send protocol data
    receiver_.readerCallback_(msg, sizeof(msg),
                              [this](const auto* buf, auto len) { writerMock_.doWrite(buf, len); });

    EXPECT_EQ(serializedMessage.length(), handler_.lengthToBeReceived());
    EXPECT_TRUE(handler_.transmissionStarted());

    std::size_t dataSplit = serializedMessage.length() / 2;

    receiver_.readerCallback_(reinterpret_cast<const u8*>(serializedMessage.c_str()), dataSplit,
                              [](const u8* buf, std::size_t len) {});
    receiver_.readerCallback_(reinterpret_cast<const u8*>(serializedMessage.c_str() + dataSplit),
                              serializedMessage.length() - dataSplit,
                              [](const u8* buf, std::size_t len) {});

    EXPECT_EQ(serializedMessage, handler_.data_);
    EXPECT_FALSE(handler_.transmissionStarted());
}

TEST_F(MessageReceiverShould, ReceiveParts)
{
    const u8 expectedAnswer[] = {message::TransmissionId::Ack};

    EXPECT_FALSE(handler_.transmissionStarted());

    std::string msg1 = "ABCD";
    std::string msg2 = "ALA MA KOTTA";

    std::vector<u8> transmission1;
    transmission1.push_back(message::TransmissionId::Start);
    transmission1.resize(transmission1.size() + 8);
    serializer::serialize(transmission1.data() + 1, msg1.length());
    transmission1.resize(transmission1.size() + msg1.length());
    memcpy(transmission1.data() + 9, msg1.c_str(), msg1.length());
    transmission1.push_back(message::TransmissionId::Start);

    std::vector<u8> transmission2;
    transmission2.resize(transmission2.size() + 8);
    serializer::serialize(transmission2.data(), msg2.length());
    transmission2.resize(transmission2.size() + msg2.length());
    memcpy(transmission2.data() + 8, msg2.c_str(), msg2.length());


    EXPECT_EQ(0, handler_.lengthToBeReceived());

    EXPECT_CALL(writerMock_, doWrite(ArrayCompare(expectedAnswer, sizeof(expectedAnswer)), 1))
        .Times(2);


    // send protocol data
    receiver_.readerCallback_(reinterpret_cast<const u8*>(transmission1.data()),
                              transmission1.size(),
                              [this](const auto* buf, auto len) { writerMock_.doWrite(buf, len); });

    EXPECT_EQ(msg1, handler_.data_);
    handler_.data_.clear();

    // next message queued so this should be cleaned
    EXPECT_EQ(0, handler_.lengthToBeReceived());
    EXPECT_TRUE(handler_.transmissionStarted());

    receiver_.readerCallback_(reinterpret_cast<const u8*>(transmission2.data()),
                              transmission2.size(),
                              [this](const auto* buf, auto len) { writerMock_.doWrite(buf, len); });

    EXPECT_EQ(msg2, handler_.data_);
    EXPECT_FALSE(handler_.transmissionStarted());
}

} // namespace handler
