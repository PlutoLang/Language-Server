#include <console.hpp>
#include <ControlInput.hpp>
#include <IpAddr.hpp>
#include <Scheduler.hpp>
#include <Socket.hpp>
#include <string.hpp>
#include <Task.hpp>

using namespace soup;

static Scheduler sched{};
static SharedPtr<Socket> sock{};

struct SendTask : public Task
{
	std::string data;

	SendTask(std::string data)
		: data(std::move(data))
	{
	}

	void onTick() final
	{
		sock->send(data);
		setWorkDone();
	}
};

static void recvLoop(Socket& s)
{
	s.recv([](Socket& s, std::string&& data, Capture&&)
	{
		string::replaceAll(data, "\r\n", "\n");
		std::cout << data << std::flush;
		recvLoop(s);
	});
}

int main()
{
	sock = sched.addSocket();
	if (!sock->connect(IpAddr("127.0.0.1"), 9170))
	{
		std::cout << "Failed to connect to 127.0.0.1:9170\n";
		return 1;
	}
	//std::cout << "Connected to 127.0.0.1:9170\n";

	recvLoop(*sock);

	std::thread stdio_thread([]
	{
		bool had_cr = false;
		while (true)
		{
			int c = getchar();
			if (c == EOF)
			{
				exit(0);
			}
			if (c == '\n')
			{
				if (!had_cr)
				{
					sched.add<SendTask>(std::string(1, '\r'));
				}
			}
			had_cr = (c == '\r');
			sched.add<SendTask>(std::string(1, (char)c));
		}
		/*console.char_handler.fp = [](char32_t c, const Capture&)
		{
			sched.add<SendTask>(std::string(1, (char)c));
		};
		console.control_handler.fp = [](ControlInput c, const Capture&)
		{
			if (c == NEW_LINE)
			{
				sched.add<SendTask>("\r\n");
			}
		};
		console.run();*/
	});
	stdio_thread.detach();

	sched.run();
}