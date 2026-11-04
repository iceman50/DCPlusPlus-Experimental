// Copyright (C) 2026 iceman50. GPL-3.0-or-later.
#include "stdinc.h"
#include <dcpp/plugins/PluginHttp.h>
#include <dcpp/plugins/PluginManager.h>
#include <dcpp/system/File.h>
#include <dcpp/net/HttpConnection.h>
#include <dcpp/settings/SettingsManager.h>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <mutex>
#include <thread>

namespace dcpp {
namespace {
constexpr size_t maxBody = 64 * 1024 * 1024;
struct Job {
	string url, destination, owner;
	std::atomic<bool> cancelled { false };
	Job(string url_, string destination_) : url(std::move(url_)), destination(std::move(destination_)), owner(PluginManager::callbackOwner()) { }
};
struct Service {
	std::mutex mutex;
	std::condition_variable ready;
	std::deque<std::shared_ptr<Job>> jobs;
	std::shared_ptr<Job> active;
	std::thread worker;
	std::atomic<bool> stopping { false };
	~Service() { stop(); }
	void stop() {
		{
			std::lock_guard<std::mutex> lock(mutex);
			stopping = true;
			jobs.clear();
		}
		ready.notify_all();
		if(worker.joinable()) worker.join();
	}
};
Service& service() { static Service value; return value; }

void event(const char* hook, const Job& job, void* data = nullptr) {
	if(!service().stopping && !job.cancelled) PluginManager::getInstance()->runHook(hook, const_cast<char*>(job.url.c_str()), data);
}

void receive(const Job& job) {
	auto checkDeadline = [&] {
		if(service().stopping || job.cancelled) throw Exception("Plugin request cancelled");
	};
	string body;
	HttpConnection connection("DC++ Plugin API");
	HttpConnection::Options options;
	options.maxBody = maxBody;
	options.timeoutMs = 60000;
	options.httpsOnly = options.sameOrigin = true;
#ifdef _WIN32
	options.checkRevocation = true;
#endif
	options.proxy = SETTING(HTTP_PROXY);
	options.cancelled = [&] { return service().stopping || job.cancelled; };
	connection.downloadBlocking(job.url, options, [&](const HttpConnection&, const uint8_t* data, size_t count) {
		if(!job.destination.empty()) body.append(reinterpret_cast<const char*>(data), count);
		DataArray chunk { const_cast<uint8_t*>(data), count };
		event(HOOK_DATAACESSOR_HTTP_RESOURCE_STREAM, job, &chunk);
	});
	if(connection.getStatusCode() != 200) throw Exception("Unexpected plugin HTTP response status");
	checkDeadline();
	if(!job.destination.empty()) {
		namespace fs = std::filesystem;
		const auto destination = fs::absolute(fs::u8path(job.destination));
		fs::path staging;
		for(unsigned attempt = 0; attempt < 32; ++attempt) {
			auto candidate = destination.parent_path() / (".dc-plugin-http-" + Util::toString(Util::rand()));
			if(fs::create_directory(candidate)) { staging = candidate; break; }
		}
		if(staging.empty()) throw Exception("Cannot stage plugin download");
		try {
			const auto temporary = staging / "download";
			File output(temporary.u8string(), File::WRITE, File::CREATE | File::TRUNCATE);
			output.write(body);
			output.close();
			checkDeadline();
#ifdef _WIN32
			if(!MoveFileExW(temporary.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
				throw Exception("Cannot publish plugin download");
#else
			fs::rename(temporary, destination);
#endif
		} catch(...) { std::error_code ignored; fs::remove_all(staging, ignored); throw; }
		std::error_code ignored;
		fs::remove(staging, ignored);
	}
}

void work() {
	auto& state = service();
	for(;;) {
		std::shared_ptr<Job> job;
		{
			std::unique_lock<std::mutex> lock(state.mutex);
			state.ready.wait(lock, [&] { return state.stopping || !state.jobs.empty(); });
			if(state.stopping) return;
			job = std::move(state.jobs.front());
			state.jobs.pop_front();
			state.active = job;
		}
		try {
			receive(*job);
			event(HOOK_DATAACESSOR_HTTP_RESOURCE_NOTIFICATION, *job);
		} catch(const Exception& e) {
			event(HOOK_DATAACESSOR_HTTP_RESOURCE_NOTIFICATION_FAILED, *job, const_cast<char*>(e.getError().c_str()));
		} catch(...) { event(HOOK_DATAACESSOR_HTTP_RESOURCE_NOTIFICATION_FAILED, *job); }
	}
}
}

bool PluginHttp::validUrl(const string& url) {
	if(url.find('#') != string::npos) return false;
	try { return HttpConnection::parseUrl(url).scheme == "https"; }
	catch(...) { return false; }
}

bool PluginHttp::submit(const string& url, const string& destination) {
	if(!validUrl(url)) return false;
	auto& state = service();
	std::lock_guard<std::mutex> lock(state.mutex);
	if(state.stopping || state.jobs.size() >= 8) return false;
	if(!state.worker.joinable()) state.worker = std::thread(work);
	state.jobs.push_back(std::make_shared<Job>(url, destination));
	state.ready.notify_one();
	return true;
}

void PluginHttp::cancelOwner(const string& owner) {
	auto& state = service();
	std::lock_guard<std::mutex> lock(state.mutex);
	if(state.active && state.active->owner == owner) state.active->cancelled = true;
	state.jobs.erase(std::remove_if(state.jobs.begin(), state.jobs.end(), [&](const auto& job) { return job->owner == owner; }), state.jobs.end());
}

void PluginHttp::stop() { service().stop(); }
}
