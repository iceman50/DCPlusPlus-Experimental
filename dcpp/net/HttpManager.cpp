/*
 * Copyright (C) 2001-2025 Jacek Sieka, arnetheduck on gmail point com
 * Copyright (C) 2026 iceman50
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "stdinc.h"
#include <dcpp/net/HttpManager.h>

#include <dcpp/text/format.h>
#include <dcpp/system/File.h>
#include <dcpp/net/HttpConnection.h>
#include <dcpp/logging/LogManager.h>
#include <dcpp/settings/SettingsManager.h>
#include <dcpp/system/Streams.h>

namespace dcpp {

using std::move;

HttpManager::HttpManager() {
	TimerManager::getInstance()->addListener(this);
}

HttpManager::~HttpManager() {
}

HttpConnection* HttpManager::download(string url, OutputStream* stream,  HttpManager::CallBack callback, const string& userAgent) {
	auto conn = makeConn(move(url), stream, callback, userAgent);
	conn->download();
	return conn;
}

HttpConnection* HttpManager::download(string url, const StringMap& postData, OutputStream* stream, HttpManager::CallBack callback, const string& userAgent) {
	auto conn = makeConn(move(url), stream, callback, userAgent);
	conn->download(postData);
	return conn;
}

HttpConnection* HttpManager::download(string url, const string& file, HttpManager::CallBack callback, const string& userAgent) {
	auto conn = makeConn(move(url), createFile(file), callback, userAgent, HttpManager::CONN_MANAGED | HttpManager::CONN_FILE);
	conn->download();
	return conn;
}

HttpConnection* HttpManager::download(std::shared_ptr<HttpConnection> connection, string url, OutputStream* stream, CallBack callback) {
	auto conn = makeConn(move(url), stream, callback, Util::emptyString, 0, move(connection));
	conn->download();
	return conn;
}

void HttpManager::disconnect(const string& url) {
	std::shared_ptr<HttpConnection> c;

	{
		Lock l(cs);
		for(auto& conn: conns) {
			if(conn.c->getUrl() == url && !conn.terminal) {
				c = conn.c;
				break;
			}
		}
	}

	if(c) {
		c->abort();
		// A custom transport may have delivered its terminal event during abort().
		{
			Lock l(cs);
			auto conn = findConn(c.get());
			if(!conn || conn->terminal)
				return;
		}
		on(HttpConnectionListener::Failed(), c.get(), _("Disconnected"));
	}
}

void HttpManager::shutdown() {
	TimerManager::getInstance()->removeListener(this);

	vector<Conn> stopped;
	{
		Lock l(cs);
		stopped = conns;
	}
	// Stop callbacks and join transports outside cs: worker callbacks acquire cs.
	for(auto& conn : stopped) {
		conn.c->removeListener(this);
		conn.c->abort();
		if(conn.isSet(HttpManager::CONN_MANAGED))
			delete conn.stream;
	}
	Lock l(cs);
	conns.clear();
}

File* HttpManager::createFile(const string& file) {
	File* f = nullptr;
	try {
		File::ensureDirectory(file);
		f = new File(file, File::RW, File::OPEN | File::CREATE | File::TRUNCATE);
	} catch(const FileException& e) {
		if(f) delete f;
		LogManager::getInstance()->message(str(F_("HTTP file download failed to create file %1%, error: %2%") % file % e.getError()),
			LogMessage::SEV_ERROR, _("HTTP downloads"));
		return nullptr;
	}
	return f;
}

HttpConnection* HttpManager::makeConn(string&& url, OutputStream* stream, HttpManager::CallBack callback, const string& userAgent,
	Flags::MaskType connFlags, std::shared_ptr<HttpConnection> connection) {
	auto c = connection ? move(connection) : std::make_shared<HttpConnection>(userAgent);
	{
		Lock l(cs);
		Conn conn { c, stream ? stream : new StringOutputStream(), callback };
		if(connFlags != 0)
			conn.setFlag(connFlags);
		if(!stream)
			conn.setFlag(HttpManager::CONN_MANAGED | HttpManager::CONN_STRING);
		conns.push_back(move(conn));
	}
	c->addListener(this);
	c->setUrl(move(url));
	fire(HttpManagerListener::Added(), c.get());
	return c.get();
}

HttpManager::Conn* HttpManager::findConn(HttpConnection* c) {
	for(auto& conn: conns) {
		if(conn.c.get() == c) {
			return &conn;
		}
	}
	return nullptr;
}

void HttpManager::resetStream(HttpConnection* c) {
	bool managed = false;
	{
		Lock l(cs);
		auto conn = findConn(c);
		if(conn->stream && conn->isSet(HttpManager::CONN_MANAGED)) {
			managed = true;
			if(conn->isSet(HttpManager::CONN_STRING)) {
				static_cast<StringOutputStream*>(conn->stream)->stringRef().clear();
			} else if(conn->isSet(HttpManager::CONN_FILE)) {
				auto file = static_cast<File*>(conn->stream);
				file->setPos(0); file->setEOF();
			}
		}
	}

	if(!managed)
		fire(HttpManagerListener::ResetStream(), c);
}

void HttpManager::removeLater(HttpConnection* c) {
	auto later = GET_TICK() + 60 * 1000;
	Lock l(cs);
	if(auto conn = findConn(c))
		conn->remove = later;
}

bool HttpManager::beginCompletion(HttpConnection* c) {
	Lock l(cs);
	auto conn = findConn(c);
	if(!conn || conn->terminal)
		return false;
	// Claim the terminal event before invoking callbacks, which may disconnect
	// this URL again. Delay reclamation until those callbacks have returned.
	conn->terminal = true;
	return true;
}

void HttpManager::on(HttpConnectionListener::Data, HttpConnection* c, const uint8_t* data, size_t len) noexcept {
	OutputStream* stream;
	{
		Lock l(cs);
		auto conn = findConn(c);
		if(!conn || conn->terminal)
			return;
		stream = conn->stream;
	}
	stream->write(data, len);
	fire(HttpManagerListener::Updated(), c, data, len);
}

void HttpManager::on(HttpConnectionListener::Failed, HttpConnection* c, const string& str) noexcept {
	if(!beginCompletion(c))
		return;
	resetStream(c);

	OutputStream* stream;
	HttpManager::CallBack callback;
	Flags::MaskType connFlags;
	{
		Lock l(cs);
		auto conn = findConn(c);
		stream = conn->stream;
		callback = conn->callback;
		connFlags = conn->getFlags();
	}
	stream->flush();
	fire(HttpManagerListener::Failed(), c, str);
	if(callback)
		callback(c, stream, connFlags | HttpManager::CONN_FAILED);
	removeLater(c);
}

void HttpManager::on(HttpConnectionListener::Complete, HttpConnection* c) noexcept {
	if(!beginCompletion(c))
		return;
	OutputStream* stream;
	HttpManager::CallBack callback;
	Flags::MaskType connFlags;
	{
		Lock l(cs);
		auto conn = findConn(c);
		stream = conn->stream;
		callback = conn->callback;
		connFlags = conn->getFlags();
	}
	stream->flush();
	fire(HttpManagerListener::Complete(), c, stream);
	if(callback)
		callback(c, stream, connFlags | HttpManager::CONN_COMPLETE);
	removeLater(c);
}

void HttpManager::on(HttpConnectionListener::Redirected, HttpConnection* c, const string& redirect) noexcept {
	resetStream(c);

	fire(HttpManagerListener::Removed(), c);
	c->setUrl(redirect);
	fire(HttpManagerListener::Added(), c);
}

void HttpManager::on(TimerManagerListener::Minute, uint64_t tick) noexcept {
	vector<pair<std::shared_ptr<HttpConnection>, OutputStream*>> removed;

	{
		Lock l(cs);
		conns.erase(std::remove_if(conns.begin(), conns.end(), [tick, &removed](const Conn& conn) -> bool {
			if(conn.remove && tick > conn.remove) {
				removed.emplace_back(conn.c, conn.isSet(HttpManager::CONN_MANAGED) ? conn.stream : nullptr);
				return true;
			}
			return false;
		}), conns.end());
	}

	for(auto& rem: removed) {
		bool replaced;
		{
			Lock l(cs);
			replaced =
				std::any_of(conns.begin(), conns.end(), [&rem](const Conn& conn) { return conn.c->getUrl() == rem.first->getUrl(); });
		}
		// TransferView identifies HTTP rows by URL. Expiring an earlier attempt
		// must not remove a newer transfer using the same stable download URL.
		if(!replaced)
			fire(HttpManagerListener::Removed(), rem.first.get());
		rem.first->removeListener(this);
		if(rem.second) {
			delete rem.second;
		}
	}
}

} // namespace dcpp
