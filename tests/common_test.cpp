#include <gtest/gtest.h>
#include <sys/eventfd.h>

#include <atomic>
#include <mutex>
#include <string>
#include <vector>

#include "common/event_loop.h"
#include "test_util.h"

using namespace mini;
using namespace std::chrono_literals;

TEST(EventLoopTest, PostedTaskRunsOnTheLoopThread) {
  LoopThread lt;
  std::atomic<int> ranOnLoopThread{-1};
  lt.loop().post([&] { ranOnLoopThread = lt.loop().inLoopThread() ? 1 : 0; });
  ASSERT_TRUE(test::waitFor([&] { return ranOnLoopThread.load() != -1; }));
  EXPECT_EQ(ranOnLoopThread, 1);
  EXPECT_FALSE(lt.loop().inLoopThread());  // we are the test thread
}

TEST(EventLoopTest, DelayedTasksRunInDeadlineOrder) {
  LoopThread lt;
  std::mutex mu;
  std::string order;
  auto append = [&](char c) {
    return [&, c] {
      std::lock_guard<std::mutex> lock(mu);
      order += c;
    };
  };
  lt.loop().postDelayed(60ms, append('b'));
  lt.loop().postDelayed(10ms, append('a'));  // posted later, due earlier
  ASSERT_TRUE(test::waitFor([&] {
    std::lock_guard<std::mutex> lock(mu);
    return order.size() == 2;
  }));
  EXPECT_EQ(order, "ab");
}

TEST(EventLoopTest, FdCallbackFiresUntilRemoved) {
  LoopThread lt;
  UniqueFd efd(::eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK));
  std::atomic<int> fired{0};
  ASSERT_EQ(lt.loop().addFd(efd.get(), EPOLLIN,
                            [&](uint32_t) {
                              uint64_t v;
                              ASSERT_EQ(::read(efd.get(), &v, sizeof(v)), 8);  // drain, or epoll re-fires
                              ++fired;
                            }),
            Status::Ok);
  uint64_t one = 1;
  ASSERT_EQ(::write(efd.get(), &one, sizeof(one)), 8);
  ASSERT_TRUE(test::waitFor([&] { return fired.load() == 1; }));

  lt.loop().removeFd(efd.get());
  ASSERT_EQ(::write(efd.get(), &one, sizeof(one)), 8);
  std::this_thread::sleep_for(100ms);
  EXPECT_EQ(fired, 1);  // no callback after removal
}

TEST(EventLoopTest, AddingTheSameFdTwiceIsRejected) {
  EventLoop loop;
  UniqueFd efd(::eventfd(0, EFD_CLOEXEC));
  EXPECT_EQ(loop.addFd(efd.get(), EPOLLIN, [](uint32_t) {}), Status::Ok);
  EXPECT_EQ(loop.addFd(efd.get(), EPOLLIN, [](uint32_t) {}), Status::InvalidArgument);
}

TEST(EventLoopTest, QuitFromAnotherThreadStopsRun) {
  EventLoop loop;
  std::thread t([&] { loop.run(); });
  std::this_thread::sleep_for(20ms);
  loop.quit();
  t.join();  // would hang forever if quit() failed to wake the loop
  SUCCEED();
}
