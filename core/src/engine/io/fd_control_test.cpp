#include <gtest/gtest.h>

#include <unistd.h>

#include <array>
#include <cerrno>

#include <engine/async.hpp>
#include <engine/sleep.hpp>
#include <utest/utest.hpp>
#include <utils/check_syscall.hpp>

#include "fd_control.hpp"

namespace {

class Pipe final {
 public:
  Pipe() { utils::CheckSyscall(::pipe(fd_), "creating pipe"); }
  ~Pipe() {
    if (fd_[0] != -1) ::close(fd_[0]);
    if (fd_[1] != -1) ::close(fd_[1]);
  }

  int ExtractIn() { return std::exchange(fd_[0], -1); }
  int ExtractOut() { return std::exchange(fd_[1], -1); }

  int In() { return fd_[0]; }
  int Out() { return fd_[1]; }

 private:
  int fd_[2];
};

bool HasTimedOut() {
  return engine::current_task::GetCurrentTaskContext()->GetWakeupSource() ==
         engine::impl::TaskContext::WakeupSource::kDeadlineTimer;
}

void CheckedWrite(int fd, const void* buf, size_t len) {
  ASSERT_EQ(len, ::write(fd, buf, len));
}

namespace io = engine::io;
using Deadline = engine::Deadline;
using FdControl = io::impl::FdControl;

constexpr std::chrono::milliseconds kReadTimeout{10};
constexpr unsigned kRepetitions = 1000;

}  // namespace

TEST(FdControl, Close) {
  RunInCoro([] {
    Pipe pipe;

    auto read_control = FdControl::Adopt(pipe.In());
    read_control->Close();
    EXPECT_EQ(-1, ::close(pipe.In()));
    EXPECT_EQ(EBADF, errno);
    EXPECT_EQ(0, ::close(pipe.Out()));
  });
}

TEST(FdControl, Ownership) {
  RunInCoro([] {
    Pipe pipe;

    {
      auto read_control = FdControl::Adopt(pipe.In());
      auto write_control = FdControl::Adopt(pipe.Out());
      EXPECT_EQ(pipe.In(), read_control->Fd());
      EXPECT_EQ(pipe.Out(), write_control->Fd());
      write_control->Invalidate();
    }

    EXPECT_EQ(-1, ::close(pipe.In()));
    EXPECT_EQ(EBADF, errno);
    EXPECT_EQ(0, ::close(pipe.Out()));
  });
}

TEST(FdControl, Wait) {
  RunInCoro([] {
    Pipe pipe;
    std::array<char, 16> buf;

    auto read_control = FdControl::Adopt(pipe.ExtractIn());
    auto& read_dir = read_control->Read();
    EXPECT_FALSE(read_dir.Wait(Deadline::FromDuration(kReadTimeout)));
    EXPECT_TRUE(HasTimedOut());

    CheckedWrite(pipe.Out(), buf.data(), 1);
    EXPECT_TRUE(read_dir.Wait(Deadline::FromDuration(kReadTimeout)));
    EXPECT_FALSE(HasTimedOut());

    EXPECT_EQ(::read(read_dir.Fd(), buf.data(), buf.size()), 1);
    EXPECT_FALSE(read_dir.Wait(Deadline::FromDuration(kReadTimeout)));
    EXPECT_TRUE(HasTimedOut());
  });
}

TEST(FdControl, Waits) {
  RunInCoro([] {
    Pipe pipe;
    auto write_control = FdControl::Adopt(pipe.ExtractOut());
    auto& write_dir = write_control->Write();

    for (unsigned i = 0; i < kRepetitions; ++i) {
      [[maybe_unused]] auto result = write_dir.Wait(Deadline::Passed());
    }
  });
}

TEST(FdControl, Destructions) {
  RunInCoro([] {
    for (unsigned i = 0; i < kRepetitions; ++i) {
      Pipe pipe;
      [[maybe_unused]] auto write_control = FdControl::Adopt(pipe.ExtractOut());
    }
  });
}

TEST(FdControl, DestructionAfterWait) {
  RunInCoro([] {
    for (unsigned i = 0; i < kRepetitions; ++i) {
      Pipe pipe;

      auto write_control = FdControl::Adopt(pipe.ExtractOut());
      auto& write_dir = write_control->Write();
      [[maybe_unused]] auto result = write_dir.Wait(Deadline::Passed());
    }
  });
}

TEST(FdControl, DestructionNoData) {
  RunInCoro([] {
    for (unsigned i = 0; i < kRepetitions; ++i) {
      Pipe pipe;

      auto read_control = FdControl::Adopt(pipe.ExtractIn());
      auto& read_dir = read_control->Read();
      auto write_control = FdControl::Adopt(pipe.ExtractOut());
      auto& write_dir = write_control->Write();

      [[maybe_unused]] auto result0 = write_dir.Wait(Deadline::Passed());
      [[maybe_unused]] auto result1 = read_dir.Wait(Deadline::Passed());
    }
  });
}

TEST(FdControl, DestructionWithData) {
  RunInCoro([] {
    for (unsigned i = 0; i < kRepetitions; ++i) {
      Pipe pipe;
      std::array<char, 16> buf;

      auto read_control = FdControl::Adopt(pipe.ExtractIn());
      auto& read_dir = read_control->Read();
      auto write_control = FdControl::Adopt(pipe.ExtractOut());
      auto& write_dir = write_control->Write();

      CheckedWrite(write_dir.Fd(), buf.data(), 1);
      [[maybe_unused]] auto result0 = write_dir.Wait(Deadline::Passed());
      [[maybe_unused]] auto result1 = read_dir.Wait(Deadline::Passed());
    }
  });
}

TEST(FdControl, PartialTransfer) {
  RunInCoro([] {
    Pipe pipe;

    auto read_control = FdControl::Adopt(pipe.ExtractIn());
    auto& read_dir = read_control->Read();

    std::array<char, 16> buf;
    io::impl::Direction::Lock lock(read_dir);
    try {
      read_dir.PerformIo(lock, &::read, buf.data(), buf.size(),
                         io::impl::TransferMode::kPartial,
                         Deadline::FromDuration(kReadTimeout), "reading");
      FAIL() << "Did not time out";
    } catch (const io::IoTimeout& ex) {
      EXPECT_EQ(0, ex.BytesTransferred());
    }

    CheckedWrite(pipe.Out(), "test", 4);
    EXPECT_EQ(
        4, read_dir.PerformIo(lock, &::read, buf.data(), buf.size(),
                              io::impl::TransferMode::kPartial, {}, "reading"));
  });
}

TEST(FdControl, WholeTransfer) {
  RunInCoro(
      [] {
        Pipe pipe;

        auto read_control = FdControl::Adopt(pipe.ExtractIn());
        auto& read_dir = read_control->Read();

        std::array<char, 16> buf;
        io::impl::Direction::Lock lock(read_dir);
        try {
          read_dir.PerformIo(lock, &::read, buf.data(), buf.size(),
                             io::impl::TransferMode::kWhole,
                             Deadline::FromDuration(kReadTimeout), "reading");
          FAIL() << "Did not time out";
        } catch (const io::IoTimeout& ex) {
          EXPECT_EQ(0, ex.BytesTransferred());
        }

        CheckedWrite(pipe.Out(), "test", 4);
        try {
          read_dir.PerformIo(lock, &::read, buf.data(), buf.size(),
                             io::impl::TransferMode::kWhole,
                             Deadline::FromDuration(kReadTimeout), "reading");
          FAIL() << "Did not time out";
        } catch (const io::IoTimeout& ex) {
          EXPECT_EQ(4, ex.BytesTransferred());
        }

        CheckedWrite(pipe.Out(), "testtesttesttesttest", 20);
        EXPECT_EQ(buf.size(),
                  read_dir.PerformIo(lock, &::read, buf.data(), buf.size(),
                                     io::impl::TransferMode::kWhole,
                                     Deadline::FromDuration(kReadTimeout),
                                     "reading"));

        std::atomic<int> read_attempts{0};
        auto counted_read = [&read_attempts](int fd, void* buf, size_t count) {
          ++read_attempts;
          return ::read(fd, buf, count);
        };
        auto sender = engine::impl::Async([fd = pipe.Out(), &read_attempts] {
          const auto deadline =
              engine::Deadline::FromDuration(kMaxTestWaitTime);
          engine::SleepFor(kReadTimeout);
          CheckedWrite(fd, "test", 4);
          while (!read_attempts) {
            ASSERT_FALSE(deadline.IsReached());
            engine::SleepFor(kReadTimeout);
          }
          CheckedWrite(fd, "testtesttesttesttest", 20);
        });
        EXPECT_EQ(buf.size(),
                  read_dir.PerformIo(lock, counted_read, buf.data(), buf.size(),
                                     io::impl::TransferMode::kWhole,
                                     Deadline::FromDuration(kMaxTestWaitTime),
                                     "reading"));
      },
      2);
}
