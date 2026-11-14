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

#include "stdafx.h"
#include <dcpp/core/header/format.h>
#include <dcpp/user/HintedUser.h>
#include <dcpp/util/Util.h>
#include <win32/util/UiServices.h>
#include <win32/util/ControlSeeds.h>
#include <win32/util/IconManager.h>
#include <win32/ChatCommands.h>
#include <win32/ChatText.h>
#include <win32/UserCommands.h>
#include <win32/util/UserPresentation.h>
#include <win32/util/LinkActions.h>
#include <win32/Notifications.h>
#include <win32/util/ShellActions.h>
#include <win32/frames/private_chat/PrivateFrame.h>
#include <win32/frames/private_transfer/PrivateSendDlg.h>

#include <win32/components/HoldRedraw.h>
#include <win32/MainWindow.h>
#include <win32/resource.h>

#include <dcpp/hub/AdcHub.h>
#include <dcpp/message/ChatMessage.h>
#include <dcpp/hub/ClientManager.h>
#include <dcpp/hub/Client.h>
#include <dcpp/connection/ConnectionManager.h>
#include <dcpp/events/LogManager.h>
#include <dcpp/plugins/PluginManager.h>
#include <dcpp/private_chat/PrivateChatManager.h>
#include <dcpp/private_chat/MessageOutboxManager.h>
#include <win32/frames/private_chat/MessageOutboxDlg.h>
#include <dcpp/message/RichText.h>
#include <dcpp/core/timer/TimerManager.h>
#include <dcpp/user/User.h>
#include <dcpp/connection/UserConnection.h>
#include <dcpp/recents/WindowInfo.h>

#include <dwt/util/StringUtils.h>

const string PrivateFrame::id = "PM";
const string& PrivateFrame::getId() const { return id; }

PrivateFrame::FrameMap PrivateFrame::frames;
CriticalSection PrivateFrame::framesMutex;

namespace {

bool matchesCurrentHub(const HintedUser& queuedUser, const HintedUser& frameUser) {
	return queuedUser.user == frameUser.user &&
		hubHintMatches(frameUser.hint, queuedUser.hint);
}

constexpr uint64_t MILLISECONDS_PER_SECOND = 1000;

int getCCPMSetting(SettingsManager::IntSetting setting, int minimum, int maximum) {
	return std::clamp(SettingsManager::getInstance()->get(setting), minimum, maximum);
}

unsigned getCCPMMaxAutomaticAttempts() {
	return static_cast<unsigned>(getCCPMSetting(SettingsManager::CCPM_MAX_AUTOMATIC_ATTEMPTS,
		SettingsManager::CCPM_AUTOMATIC_ATTEMPTS_MIN, SettingsManager::CCPM_AUTOMATIC_ATTEMPTS_MAX));
}

uint64_t getCCPMStableConnectionTime() {
	return static_cast<uint64_t>(getCCPMSetting(SettingsManager::CCPM_STABLE_CONNECTION_TIME,
		SettingsManager::CCPM_STABLE_CONNECTION_TIME_MIN,
		SettingsManager::CCPM_STABLE_CONNECTION_TIME_MAX)) * MILLISECONDS_PER_SECOND;
}

uint32_t getCCPMReconnectDelay(unsigned attempts) {
	// Jitter prevents two identical clients from remaining synchronized after
	// both sides lose and immediately try to recreate the same channel.
	const auto baseSeconds = getCCPMSetting(SettingsManager::CCPM_RECONNECT_BASE_DELAY,
		SettingsManager::CCPM_RECONNECT_DELAY_MIN, SettingsManager::CCPM_RECONNECT_DELAY_MAX);
	const auto maxSeconds = std::max(baseSeconds, getCCPMSetting(SettingsManager::CCPM_RECONNECT_MAX_DELAY,
		SettingsManager::CCPM_RECONNECT_DELAY_MIN, SettingsManager::CCPM_RECONNECT_DELAY_MAX));
	const auto baseDelay = static_cast<uint64_t>(baseSeconds) * MILLISECONDS_PER_SECOND;
	const auto maxDelay = static_cast<uint64_t>(maxSeconds) * MILLISECONDS_PER_SECOND;
	const auto shift = std::min(attempts > 0 ? attempts - 1 : 0, 4u);
	const auto delay = std::min(baseDelay << shift, maxDelay);
	const auto jitterLimit = static_cast<uint32_t>(std::min(delay / 4, maxDelay - delay));
	return static_cast<uint32_t>(delay + Util::rand(0, jitterLimit));
}

} // namespace

void PrivateFrame::openWindow(TabViewPtr parent, const HintedUser& replyTo_, const tstring& msg,
	const string& logPath, bool activate)
{
	PrivateFrame* frame = nullptr;
	{
		Lock l(framesMutex);
		auto i = frames.find(replyTo_);
		if(i != frames.end()) {
			frame = i->second;
		}
	}

	const auto existing = frame != nullptr;
	if(!frame) {
		frame = new PrivateFrame(parent, replyTo_, logPath);
	}
	if(existing && !replyTo_.hint.empty() &&
		!hubHintsEqual(frame->replyTo.getUser().hint, replyTo_.hint))
	{
		frame->changeHub(replyTo_.hint);
	}
	if(activate)
		frame->activate();
	if(!msg.empty())
		frame->sendMessage(msg);
}

bool PrivateFrame::privateSendNotice(TabViewPtr parent, const HintedUser& peer, const tstring& text) {
	PrivateFrame* frame = nullptr;
	size_t count;
	{
		Lock l(framesMutex);
		auto i = frames.find(peer.user);
		if(i != frames.end())
			frame = i->second;
		count = frames.size();
	}

	if(!frame) {
		if(static_cast<int>(count) >= SETTING(MAX_PM_WINDOWS))
			return false;
		frame = new PrivateFrame(parent, peer);
	}

	// File names are plain local status text, never commands, links or outgoing PMs.
	// Keep private file metadata out of the normal chat log and plugin hooks.
	frame->addChatPlain(Text::toT("[" + Util::getShortTimeString() + "] ") + text);
	frame->setDirty(SettingsManager::BOLD_PM);
	return true;
}

bool PrivateFrame::gotMessage(TabViewPtr parent, const ChatMessage& message, const string& hubHint, bool fromBot) {
	auto& user = (message.replyTo == ClientManager::getInstance()->getMe()) ? message.to : message.replyTo;

	PrivateFrame* frame = nullptr;
	size_t frameCount;
	{
		Lock l(framesMutex);
		auto i = frames.find(user);
		if(i != frames.end()) {
			frame = i->second;
		}
		frameCount = frames.size();
	}

	if(!frame) {
		// creating a new window

		if(static_cast<int>(frameCount) >= SETTING(MAX_PM_WINDOWS)) {
			return false;
		}

		auto hintedUser = HintedUser(user, hubHint);

		auto p = new PrivateFrame(parent, hintedUser);
		if(!SETTING(POPUNDER_PM))
			p->activate();

		p->addStatus(Text::toT(str(F_("Message through hub: %1%") % ClientManager::getInstance()->getHubName(hintedUser))));

		p->addChat(message);
		p->lastMessageTime = message.timestamp;

		if(Util::getAway() && !(SETTING(NO_AWAYMSG_TO_BOTS) && fromBot)) {
			auto awayMessage = Util::getAwayMessage();
			if(!awayMessage.empty()) {
				p->sendMessage(Text::toT(awayMessage));
			}
		}

		Notifications::notify(Notifications::NOTIFICATION_PM_WINDOW, Text::toT(message.message), [user] { activateWindow(user); });

	} else {
		// send the message to the existing window
		frame->addChat(message);
		frame->lastMessageTime = message.timestamp;
	}

	Notifications::notify(Notifications::NOTIFICATION_PM, Text::toT(message.message), [user] { activateWindow(user); });

	return true;
}

void PrivateFrame::activateWindow(const UserPtr& u) {
	PrivateFrame* frame = nullptr;
	{
		Lock l(framesMutex);
		auto i = frames.find(u);
		if(i != frames.end()) {
			frame = i->second;
		}
	}
	if(frame) {
		frame->activate();
	}
}

bool PrivateFrame::isOpen(const UserPtr& u) {
	Lock l(framesMutex);
	return frames.find(u) != frames.end();
}

void PrivateFrame::refreshRichTextSettings() {
	vector<PrivateFrame*> openFrames;
	{
		Lock l(framesMutex);
		openFrames.reserve(frames.size());
		for(const auto& frame: frames) openFrames.push_back(frame.second);
	}
	for(auto frame: openFrames) frame->updateRichTextAvailability();
}

void PrivateFrame::handlePMConnection(const HintedUser& user, const string& connectionToken) {
	PrivateFrame* frame = nullptr;
	{
		Lock l(framesMutex);
		auto i = frames.find(user.user);
		if(i != frames.end()) {
			frame = i->second;
		}
	}

	if(frame) {
		frame->adoptPMConnection(connectionToken);
	}
	// With no frame, PrivateChatManager remains the continuous listener and
	// keeps the exact channel safely parked. A delayed availability task must
	// not undo an intentional return performed while the tab was closing.
}

void PrivateFrame::handlePMMessage(const HintedUser& user, const string& connectionToken,
	const ChatMessage& message)
{
	if(message.from == ClientManager::getInstance()->getMe()) {
		return;
	}

	PrivateFrame* frame = nullptr;
	{
		Lock l(framesMutex);
		auto i = frames.find(user.user);
		if(i != frames.end()) {
			frame = i->second;
		}
	}
	if(!frame) {
		return;
	}

	Lock lifetimeLock(frame->connLifetimeMutex);
	{
		Lock l(frame->mutex);
		if(!frame->conn.load() || frame->connToken != connectionToken) {
			return;
		}
	}
	frame->messageSeenPending = true;
	frame->sendSeenIfActive();
}

void PrivateFrame::handlePMI(const HintedUser& user, const string& connectionToken, const AdcCommand& cmd) {
	PrivateFrame* frame = nullptr;
	{
		Lock l(framesMutex);
		auto i = frames.find(user.user);
		if(i != frames.end()) {
			frame = i->second;
		}
	}

	if(!frame) {
		if(cmd.hasFlag("QU", 0)) {
			PrivateChatManager::getInstance()->releasePMConn(user.user, connectionToken, true);
		}
		return;
	}

	uint64_t revision;
	{
		Lock l(frame->mutex);
		if(!frame->conn.load() || frame->connToken != connectionToken) {
			if(cmd.hasFlag("QU", 0)) {
				PrivateChatManager::getInstance()->releasePMConn(user.user, connectionToken, true);
			}
			return;
		}
		revision = frame->connRevision.load();
	}

	if(!frame->isCurrentConnection(connectionToken, revision)) return;
	string value;
	if(cmd.hasFlag("SN", 0)) {
		frame->updatePMInfo(PM_INFO_SEEN);
	}
	if(cmd.getParam("TP", 0, value) && (value == "0" || value == "1")) {
		frame->updatePMInfo(value == "1" ? PM_INFO_TYPING_ON : PM_INFO_TYPING_OFF);
	}
	if(cmd.getParam("AC", 0, value) && value == "0") {
		frame->updatePMInfo(PM_INFO_NO_AUTOCONNECT);
	}
	if(cmd.hasFlag("QU", 0)) {
		frame->updatePMInfo(PM_INFO_QUIT);
	}
}

void PrivateFrame::closeAll(bool offline) {
	vector<PrivateFrame*> snapshot;
	{
		Lock l(framesMutex);
		snapshot.reserve(frames.size());
		for(const auto& i: frames) {
			snapshot.push_back(i.second);
		}
	}

	for(auto frame: snapshot) {
		if(!offline || !frame->online) {
			frame->close(true);
		}
	}
}

WindowParams PrivateFrame::getWindowParams() const {
	WindowParams ret;
	addRecentParams(ret);
	ret["CID"] = WindowParam(replyTo.getUser().user->getCID().toBase32(), WindowParam::FLAG_IDENTIFIES | WindowParam::FLAG_CID);
	ret["Hub"] = WindowParam(replyTo.getUser().hint);
	ret["LogPath"] = WindowParam(getLogPath());
	return ret;
}

void PrivateFrame::parseWindowParams(TabViewPtr parent, const WindowParams& params) {
	auto cid = params.find("CID");
	auto hub = params.find("Hub");
	if(cid != params.end() && hub != params.end()) {
		auto logPath = params.find("LogPath");
		openWindow(parent, HintedUser(ClientManager::getInstance()->getUser(CID(cid->second)), hub->second), Util::emptyStringT,
			logPath != params.end() ? logPath->second.content : Util::emptyString, parseActivateParam(params));
	}
}

bool PrivateFrame::isFavorite(const WindowParams& params) {
	auto cid = params.find("CID");
	if(cid != params.end()) {
		UserPtr u = ClientManager::getInstance()->getUser(CID(cid->second));
		if(u)
			return FavoriteManager::getInstance()->isFavoriteUser(u);
	}
	return false;
}

PrivateFrame::PrivateFrame(TabViewPtr parent, const HintedUser& replyTo_, const string& logPath) :
BaseType(parent, _T(""), IDH_PM, IDI_PRIVATE_OFF, false),
replyTo(replyTo_),
online(false),
conn(nullptr),
connRevision(0),
connEstablishedTick(0),
acceptCCPMConnections(true),
localTyping(false),
remoteTyping(false),
messageSeenPending(false),
allowAutoCCPM(true),
nextAutoCCPMAttempt(0),
autoCCPMAttempts(0),
autoCCPMReconnectScheduled(false),
lastMessageTime(time(NULL))
{
	createChat(this);
	chat->setHelpId(IDH_PM_CHAT);
	addWidget(chat);
	chat->onContextMenu([this](const dwt::ScreenCoordinate &sc) { return handleChatContextMenu(sc); });
	chat->onFocus([this] { sendSeenIfActive(); });

	message->setHelpId(IDH_PM_MESSAGE);
	addWidget(message, ALWAYS_FOCUS);
	addWidget(richTextButton, AUTO_FOCUS, false);
	message->onKeyDown([this](int c) { return handleMessageKeyDown(c); });
	message->onSysKeyDown([this](int c) { return handleMessageKeyDown(c); });
	message->onChar([this](int c) { return handleMessageChar(c); });
	message->onUpdated([this] { updateTypingState(); });
	message->onFocus([this] {
		updateTypingState();
		sendSeenIfActive();
	});
	message->onKillFocus([this](dwt::Widget*) { updateTypingState(false); });
	onVisibilityChanged([this](bool visible) {
		if(visible) {
			sendSeenIfActive();
			updateTypingState();
		} else {
			updateTypingState(false);
		}
	});

	initStatus();

	status->onDblClicked(STATUS_STATUS, [this] { openLog(); });
	status->onClicked(STATUS_PRIVATE_SEND, [this] { handlePrivateSend(); });
	status->setToolTip(STATUS_PRIVATE_SEND, T_("Private Send - click to send or review files"));
	// Capability registration and contact verification can change independently
	// of the PM frame's connection notifications.
	setTimer([this] { updatePrivateSendAvailability(); return true; }, 1000, TIMER_PRIVATE_SEND);

	{
		auto f = [this] { handleChannelMenu(); };
		status->onClicked(STATUS_CHANNEL, f);
		status->onRightClicked(STATUS_CHANNEL, f);
	}

	status->setToolTip(STATUS_CHANNEL, T_("Current communication channel - click to change"));

	status->setHelpId(STATUS_STATUS, IDH_PM_STATUS);
	status->setHelpId(STATUS_CHANNEL, IDH_PM_CHANNEL);

	initAccels();

	layout();

	readLog(logPath, SETTING(PM_LAST_LOG_LINES));

	// Publish before registering/adopting so PrivateChatManager can park a
	// connection that overlaps construction. Its Connected callback always
	// parks first, making getPMConn below an atomic handoff under its lock.
	{
		Lock l(framesMutex);
		frames.emplace(replyTo.getUser(), this);
	}

	ConnectionManager::getInstance()->addListener(this);
	{
		Lock lifetimeLock(connLifetimeMutex);
		Lock l(mutex);
		auto activeConn = PrivateChatManager::getInstance()->getPMConn(replyTo.getUser().user);
		if(activeConn && !matchesCurrentHub(activeConn->getHintedUser(), replyTo.getUser())) {
			// Parked channels are hub-scoped just like live Connected events.
			// Do not silently reuse a channel from another hub in this frame.
			activeConn->disconnect(true);
			activeConn = nullptr;
		}
		if(activeConn && replyTo.getUser().hint.empty()) {
			replyTo.getUser().hint = activeConn->getHintedUser().hint;
		}
		conn.store(activeConn);
		if(activeConn) {
			connToken = activeConn->getToken();
			connEstablishedTick = GET_TICK();
			outboxRequireDirect = true;
			++connRevision;
		}
	}

	ClientManager::getInstance()->addListener(this);
	callAsync([this] {
		updateOnlineStatus(true);
	});

	addRecent();
}

PrivateFrame::~PrivateFrame() {
}

void PrivateFrame::addedChat(const tstring& message) {
	setDirty(SettingsManager::BOLD_PM);

	if (ccReady() && SETTING(DONT_LOG_CCPM)) return;

	if(SETTING(LOG_PRIVATE_CHAT)) {
		ParamMap params;
		params["message"] = [&message] { return Text::toDOS(Text::fromT(message)); };
		fillLogParams(params);
		LOG(LogManager::PM, params);
	}
}

void PrivateFrame::addStatus(const tstring& text) {
	lastStatus = Text::toT("[" + Util::getShortTimeString() + "] ") + text;
	if(!remoteTyping) {
		status->setText(STATUS_STATUS, lastStatus);
	}

	if(SETTING(STATUS_IN_CHAT)) {
		addChat(_T("*** ") + text);
	} else {
		setDirty(SettingsManager::BOLD_PM);
	}
}

bool PrivateFrame::preClosing() {
	cancelCCPMAttempt();
	setTimer(nullptr, 0, TIMER_PRIVATE_SEND);
	updateTypingState(false);
	cancelAutoCCPMReconnect();
	{
		Lock lifetimeLock(connLifetimeMutex);
		UserConnection* activeConn;
		string activeToken;
		{
			Lock l(mutex);
			acceptCCPMConnections = false;
			allowAutoCCPM = false;
			activeConn = conn.exchange(nullptr);
			activeToken = connToken;
			connToken.clear();
			connEstablishedTick = 0;
			++connRevision;
		}

		// Stop advertising this frame before removing its ConnectionManager
		// listener. Otherwise PrivateChatManager could leave a new connection
		// for a frame that can no longer adopt it.
		{
			Lock l(framesMutex);
			frames.erase(replyTo.getUser());
		}

		if(activeConn) {
			const auto canPark = activeConn->supportsCPMI() &&
				activeConn->getState() == UserConnection::STATE_CMD;
			if(canPark) {
				activeConn->pmi("QU", "1");
			}
			// A failed connection is already being removed and must not be
			// returned to PrivateChatManager while its removal event is in flight.
			if(canPark) {
				if(!PrivateChatManager::getInstance()->returnPMConn(
					replyTo.getUser().user, activeToken, activeConn) &&
					activeConn->getState() == UserConnection::STATE_CMD)
				{
					activeConn->disconnect(false);
				}
			} else if(activeConn->getState() != UserConnection::STATE_UNCONNECTED) {
				activeConn->disconnect(false);
			}
		}
	}

	ClientManager::getInstance()->removeListener(this);
	ConnectionManager::getInstance()->removeListener(this);

	return true;
}

string PrivateFrame::getLogPath() const {
	ParamMap params;
	fillLogParams(params);
	return Util::validateFileName(LogManager::getInstance()->getPath(LogManager::PM, params));
}

void PrivateFrame::openLog() {
	ShellActions::openFile(Text::toT(getLogPath()));
}

void PrivateFrame::fillLogParams(ParamMap& params) const {
	params["hubNI"] = [this] { return ClientManager::getInstance()->getHubName(replyTo.getUser()); };
	params["hubURL"] = [this] { return replyTo.getUser().hint; };
	params["userCID"] = [this] { return replyTo.getUser().user->getCID().toBase32(); };
	params["userNI"] = [this] { return ClientManager::getInstance()->getNick(replyTo.getUser()); };
	params["myCID"] = [] { return ClientManager::getInstance()->getMe()->getCID().toBase32(); };
}

void PrivateFrame::layout() {
	const int border = 2;

	dwt::Rectangle r { getClientSize() };

	r.size.y -= status->refresh();

	int ymessage = message->getTextSize(_T("A")).y * messageLines + 10;
	dwt::Rectangle rm(0, r.size.y - ymessage, r.width(), ymessage);
	if(richTextButton->getVisible()) {
		const auto preferred = richTextButton->getPreferredSize();
		const auto buttonWidth = std::min(preferred.x, std::max(0L, rm.width() / 3));
		auto messageRect = rm;
		messageRect.size.x = std::max(0L, rm.width() - buttonWidth - border);
		message->resize(messageRect);
		const auto buttonHeight = std::min(preferred.y, rm.height());
		richTextButton->resize(dwt::Rectangle(messageRect.width() + border,
			rm.y() + (rm.height() - buttonHeight) / 2, buttonWidth, buttonHeight));
	} else {
		message->resize(rm);
	}

	r.size.y -= rm.size.y + border;
	chat->resize(r);
}

void PrivateFrame::updateOnlineStatus(bool newChannel) {
	auto hubs = ClientManager::getInstance()->getHubUrls(replyTo.getUser());
	auto& user = replyTo.getUser();

	if(user.hint.empty() && !hubs.empty()) {
		Lock l(mutex);
		if(user.hint.empty()) {
			user.hint = hubs.front();
			newChannel = true;
		}
	}

	auto hintOnline = !hubs.empty() && (user.hint.empty() ||
		any_of(hubs.begin(), hubs.end(), [&user](const string& hubUrl) {
			return hubHintsEqual(user.hint, hubUrl);
		}));

	if(newChannel || online != hintOnline) {
		online = hintOnline;

		if(!newChannel) {
			addStatus(online ? T_("User went online") : T_("User went offline"));
		}
		if(!online) {
			cancelCCPMAttempt();
			cancelAutoCCPMReconnect();
			nextAutoCCPMAttempt = 0;
			autoCCPMAttempts = 0;
		}
		setIcon(online ? IDI_PRIVATE : IDI_PRIVATE_OFF);
		status->setIcon(STATUS_CHANNEL, IconManager::statusIcon(ccReady() ? IDI_SECURE : online ? IDI_HUB : IDI_HUB_OFF));
		newChannel = true;
	}

	setText(UserPresentation::getNick(replyTo.getUser()) + _T(" - ") + UserPresentation::getHubName(replyTo.getUser()));

	if(newChannel) {
		updateChannel();

		if(online && allowAutoCCPM && SETTING(ALWAYS_CCPM) && !ccReady()) {
			startCC(true);
		}
	}

	updateRichTextAvailability();
}

void PrivateFrame::updateChannel() {
	auto channel = ccReady() ? T_("Direct encrypted channel") : UserPresentation::getHubName(replyTo.getUser());
	dwt::util::cutStr(channel, 26);
	status->setText(STATUS_CHANNEL, channel, true);
}

void PrivateFrame::updateRichTextAvailability() {
	bool directRoute = false;
	bool available = false;
	bool temporarySharing = online;
	auto routeHubUrl = replyTo.getUser().hint;
	{
		Lock lifetimeLock(connLifetimeMutex);
		UserConnection* activeConn;
		{
			Lock l(mutex);
			activeConn = conn.load();
		}
		if(activeConn && activeConn->getState() == UserConnection::STATE_CMD && activeConn->isSecure()) {
			directRoute = true;
			temporarySharing = true;
			available = activeConn->supportsRTF0();
			routeHubUrl = activeConn->getHubUrl();
		}
	}
	if(!directRoute) {
		available = ClientManager::getInstance()->supportsRichText(replyTo.getUser());
	}
	setChatAvailability(available, temporarySharing, routeHubUrl);
	updatePrivateSendAvailability();
}

void PrivateFrame::updatePrivateSendAvailability() {
	// The dialog also sets up contact approval, so it must be accessible before
	// a peer is approved or a direct channel is connected.
	const bool ready = SETTING(ENABLE_PRIVATE_SEND) && DirectTransferManager::available();
	if(ready != privateSendReady) {
		privateSendReady = ready;
		status->setIcon(STATUS_PRIVATE_SEND, ready ? IconManager::statusIcon(IDI_FILE) : nullptr, true);
	}
}

void PrivateFrame::handlePrivateSend() {
	// Recheck on click in case the channel or contact approval just changed.
	updatePrivateSendAvailability();
	if(privateSendReady && UiServices::mainWindow) {
		if(online && !ccReady() && ccpmAttemptToken.empty()) startCC();
		UiServices::mainWindow->openPrivateSend(replyTo.getUser());
	}
}

void PrivateFrame::startCC(bool silent) {
	if(!SETTING(ENABLE_CCPM)) {
		cancelCCPMAttempt();
		cancelAutoCCPMReconnect();
		if(!silent) addStatus(T_("Direct encrypted channels are disabled in settings"));
		return;
	}
	if(!ccpmAttemptToken.empty()) {
		if(!silent) addStatus(T_("A direct encrypted channel connection is already in progress"));
		return;
	}
	if(!silent) {
		{
			Lock l(mutex);
			acceptCCPMConnections = true;
		}
		// An explicit request closes the circuit and starts a fresh retry budget.
		allowAutoCCPM = true;
		autoCCPMAttempts = 0;
		nextAutoCCPMAttempt = 0;
		cancelAutoCCPMReconnect();
	} else if(!online || !allowAutoCCPM || !SETTING(ALWAYS_CCPM)) {
		return;
	}

	if(ccReady()) {
		if(!silent) { addStatus(T_("A direct encrypted channel is already available")); }
		cancelAutoCCPMReconnect();
		return;
	}

	const auto now = GET_TICK();
	if(silent) {
		if(autoCCPMAttempts >= getCCPMMaxAutomaticAttempts()) {
			pauseAutoCCPMReconnect();
			return;
		}
		if(nextAutoCCPMAttempt > now) {
			scheduleAutoCCPMReconnect();
			return;
		}
	}

	{
		auto lock = ClientManager::getInstance()->lock();
		const auto& user = replyTo.getUser();
		auto ou = user.hint.empty() ?
			ClientManager::getInstance()->findOnlineUser(user) :
			ClientManager::getInstance()->findOnlineUserHint(user);
		if(!ou) {
			if(!silent) { addStatus(T_("User offline")); }
			return;
		}

		tstring err = ou->getUser()->isNMDC() ? T_("A secure ADC hub is required; this feature is not supported on NMDC hubs") :
			!ou->getUser()->isSet(User::TLS) ? T_("The user does not support secure encrypted connections") :
			!ou->getIdentity().supports(AdcHub::CCPM_FEATURE) ? T_("The user does not support the CCPM ADC extension") : _T("");
		if(!err.empty()) {
			if(!silent) { addStatus(str(TF_("Cannot start the direct encrypted channel: %1%") % err)); }
			return;
		}
	}

	if(!silent) { addStatus(T_("Establishing a direct encrypted channel...")); }
	if(silent) {
		++autoCCPMAttempts;
	}
	{
		Lock l(mutex);
		acceptCCPMConnections = true;
	}
	cancelAutoCCPMReconnect();
	ccpmAttemptToken = ConnectionManager::getInstance()->makeToken();
	const auto token = ccpmAttemptToken;
	setTimer([this, token] {
		callAsync([this, token] { failCCPMAttempt(token, T_("Connection timed out after 30 seconds")); });
		return false;
	}, static_cast<unsigned>(ConnectionManager::PM_CONNECT_TIMEOUT), TIMER_CCPM_CONNECT);
	ClientManager::getInstance()->connect(replyTo.getUser(), token, CONNECTION_TYPE_PM);
}

void PrivateFrame::cancelCCPMAttempt() {
	setTimer(nullptr, 0, TIMER_CCPM_CONNECT);
	const auto token = std::exchange(ccpmAttemptToken, string());
	if(!token.empty()) ConnectionManager::getInstance()->cancelPMConnect(replyTo.getUser(), token);
}

void PrivateFrame::failCCPMAttempt(const string& token, const tstring& reason) {
	if(token != ccpmAttemptToken || token.empty()) return;
	cancelCCPMAttempt();
	{
		Lock lifetimeLock(connLifetimeMutex);
		Lock l(mutex);
		auto activeConn = conn.load();
		if(activeConn && activeConn->getState() == UserConnection::STATE_CMD && activeConn->isSecure()) return;
		acceptCCPMConnections = false;
	}
	addStatus(str(TF_("Cannot establish the direct encrypted channel: %1%") % reason));
	nextAutoCCPMAttempt = GET_TICK() + getCCPMReconnectDelay(autoCCPMAttempts);
	scheduleAutoCCPMReconnect();
}

void PrivateFrame::on(ConnectionManagerListener::Failed, ConnectionQueueItem* cqi, const string& error) noexcept {
	if(cqi->getType() != CONNECTION_TYPE_PM || !matchesCurrentHub(cqi->getUser(), replyTo.getUser())) return;
	const auto token = cqi->getToken();
	callAsync([this, token, error] { failCCPMAttempt(token, Text::toT(error)); });
}

void PrivateFrame::closeCC(bool silent) {
	cancelCCPMAttempt();
	if(!silent) outboxRequireDirect = false;
	allowAutoCCPM = false;
	nextAutoCCPMAttempt = 0;
	cancelAutoCCPMReconnect();
	bool found = false;
	{
		Lock lifetimeLock(connLifetimeMutex);
		UserConnection* activeConn;
		{
			Lock l(mutex);
			acceptCCPMConnections = false;
			activeConn = conn.load();
			if(activeConn && activeConn->getState() != UserConnection::STATE_CMD) {
				activeConn = nullptr;
			}
		}
		if(activeConn) {
			if(activeConn->supportsCPMI()) {
				activeConn->pmi("AC", "0");
			}
			activeConn->disconnect(false);
			found = true;
		}
	}

	if(found) {
		cancelAutoCCPMReconnect();
		if(!silent) { addStatus(T_("Disconnecting the direct encrypted channel...")); }
	} else {
		if(!silent) { addStatus(T_("No direct encrypted channel available")); }
	}
}

void PrivateFrame::cancelAutoCCPMReconnect() {
	if(autoCCPMReconnectScheduled) {
		setTimer(nullptr, 0, TIMER_CCPM_RECONNECT);
		autoCCPMReconnectScheduled = false;
	}
}

void PrivateFrame::scheduleAutoCCPMReconnect() {
	if(!online || !allowAutoCCPM || !SETTING(ENABLE_CCPM) || !SETTING(ALWAYS_CCPM) || ccReady() || !ccpmAttemptToken.empty()) {
		cancelAutoCCPMReconnect();
		return;
	}

	const auto now = GET_TICK();
	if(nextAutoCCPMAttempt <= now) {
		nextAutoCCPMAttempt = now + getCCPMReconnectDelay(autoCCPMAttempts);
	}
	const auto delay = static_cast<unsigned>(nextAutoCCPMAttempt - now);
	autoCCPMReconnectScheduled = true;
	setTimer([this] {
		autoCCPMReconnectScheduled = false;
		// Returning false kills this timer ID. Queue the attempt so startCC can
		// safely arm the same timer again after this callback returns.
		callAsync([this] { startCC(true); });
		return false;
	}, std::max(1u, delay), TIMER_CCPM_RECONNECT);
}

void PrivateFrame::pauseAutoCCPMReconnect() {
	if(!allowAutoCCPM) {
		return;
	}
	allowAutoCCPM = false;
	cancelAutoCCPMReconnect();
	addStatus(T_("Automatic direct channel reconnection was paused after repeated failures; use the channel menu to try again"));
}

void PrivateFrame::changeHub(const string& hubHint) {
	outboxRequireDirect = false;
	{
		// Keep the close request and hint change atomic with respect to a
		// connection event for the old hub.
		Lock lifetimeLock(connLifetimeMutex);
		closeCC(true);
		{
			Lock l(mutex);
			replyTo.getUser().hint = hubHint;
			acceptCCPMConnections = true;
		}
	}
	updateOnlineStatus(true);
}

void PrivateFrame::adoptPMConnection(const string& connectionToken) {
	uint64_t revision = 0;
	bool adopted = false;
	{
		Lock lifetimeLock(connLifetimeMutex);
		Lock l(mutex);
		if(!acceptCCPMConnections) {
			PrivateChatManager::getInstance()->releasePMConn(
				replyTo.getUser().user, connectionToken, true);
			return;
		}

		auto activeConn = conn.load();
		if(activeConn) {
			if(connToken == connectionToken) {
				// ConnectionManager may have called this frame before
				// PrivateChatManager parked its fallback copy.
				PrivateChatManager::getInstance()->releasePMConn(
					replyTo.getUser().user, connectionToken, false);
			}
			return;
		}

		activeConn = PrivateChatManager::getInstance()->getPMConn(
			replyTo.getUser().user, connectionToken);
		if(!activeConn) {
			return;
		}

		if(!matchesCurrentHub(activeConn->getHintedUser(), replyTo.getUser())) {
			activeConn->disconnect(true);
			return;
		}

		if(replyTo.getUser().hint.empty()) {
			replyTo.getUser().hint = activeConn->getHintedUser().hint;
		}
		conn.store(activeConn);
		connToken = connectionToken;
		connEstablishedTick = GET_TICK();
		outboxRequireDirect = true;
		revision = ++connRevision;
		adopted = true;
	}

	if(adopted) {
		callAsync([this, connectionToken, revision] {
			if(isCurrentConnection(connectionToken, revision)) {
				cancelCCPMAttempt();
				cancelAutoCCPMReconnect();
				nextAutoCCPMAttempt = 0;
				localTyping = false;
				remoteTyping = false;
				updateOnlineStatus(true);
				addStatus(T_("A direct encrypted channel has been established"));
				updateTypingState();
				sendSeenIfActive();
			}
		});
	}
}

bool PrivateFrame::ccReady() const {
	Lock lifetimeLock(connLifetimeMutex);
	UserConnection* activeConn;
	{
		Lock l(mutex);
		activeConn = conn.load();
	}
	return activeConn && activeConn->getState() == UserConnection::STATE_CMD && activeConn->isSecure();
}

bool PrivateFrame::isCurrentConnection(const string& token, uint64_t revision) const {
	Lock l(mutex);
	return conn.load() && connToken == token && connRevision.load() == revision;
}

bool PrivateFrame::isDisconnectedConnection(uint64_t revision) const {
	Lock l(mutex);
	return !conn.load() && connRevision.load() == revision;
}

void PrivateFrame::updatePMInfo(PMInfo type) {
	switch(type) {
	case PM_INFO_SEEN:
		addStatus(T_("Message seen"));
		break;
	case PM_INFO_TYPING_ON:
		remoteTyping = true;
		status->setText(STATUS_STATUS, T_("User is typing..."));
		break;
	case PM_INFO_TYPING_OFF:
		remoteTyping = false;
		status->setText(STATUS_STATUS, lastStatus);
		break;
	case PM_INFO_NO_AUTOCONNECT:
		allowAutoCCPM = false;
		cancelAutoCCPMReconnect();
		break;
	case PM_INFO_QUIT:
		remoteTyping = false;
		addStatus(T_("User closed the private message window"));
		break;
	}
}

bool PrivateFrame::sendPMI(const char* name, const string& value) {
	Lock lifetimeLock(connLifetimeMutex);
	UserConnection* activeConn;
	{
		Lock l(mutex);
		activeConn = conn.load();
	}
	if(activeConn && activeConn->getState() == UserConnection::STATE_CMD &&
		activeConn->isSecure() && activeConn->supportsCPMI())
	{
		activeConn->pmi(name, value);
		return true;
	}
	return false;
}

void PrivateFrame::updateTypingState(bool refreshTimeout) {
	bool cpmiReady;
	{
		Lock lifetimeLock(connLifetimeMutex);
		UserConnection* activeConn;
		{
			Lock l(mutex);
			activeConn = conn.load();
		}
		cpmiReady = activeConn && activeConn->getState() == UserConnection::STATE_CMD &&
			activeConn->isSecure() && activeConn->supportsCPMI();
	}
	const auto typing = cpmiReady && refreshTimeout && !message->getText().empty() && message->hasFocus() && isActive();
	if(typing != localTyping) {
		localTyping = typing;
		sendPMI("TP", typing ? "1" : "0");
	}

	if(typing) {
		setTimer([this] {
			updateTypingState(false);
			return false;
		}, 5000, TIMER_CPMI_TYPING);
	} else {
		setTimer(nullptr, 0, TIMER_CPMI_TYPING);
	}
}

void PrivateFrame::sendSeenIfActive() {
	if(messageSeenPending && isActive() && UiServices::mainWindow->onForeground()) {
		if(sendPMI("SN", "1")) {
			messageSeenPending = false;
		}
	}
}

void PrivateFrame::enterImpl(const tstring& s) {
	bool resetText = true;
	bool send = false;

	// Process special commands
	if(s[0] == '/') {
		tstring cmd = s;
		tstring param;
		tstring message;
		tstring status;
		bool thirdPerson = false;

		if(PluginManager::getInstance()->onChatCommandPM(replyTo.getUser(), Text::fromT(s))) {
			// Plugins, chat commands

		} else if(ChatCommands::checkCommand(cmd, param, message, status, thirdPerson)) {
			if(!message.empty()) {
				if(!sendMessage(message, thirdPerson, false, true)) {
					addStatus(T_("The message could not be sent because the active route changed."));
					resetText = false;
				}
			}
			if(!status.empty()) {
				addStatus(status);
			}
		} else if(ChatType::checkCommand(cmd, param, status)) {
			if(!status.empty()) {
				addStatus(status);
			}
		} else if(Util::stricmp(cmd.c_str(), _T("rtf")) == 0) {
			if(param.empty()) {
				addStatus(T_("Usage: /rtf <message>"));
			} else if(online || ccReady()) {
				if(!sendMessage(param, false, true)) {
					addStatus(T_("RTF0 is unsupported or invalid, contains non-magnet inline media, or references an attachment that is not available in this hub's share. The message was not sent."));
					resetText = false;
				}
			} else {
				this->message->showPopup(T_("User offline"), T_("The message cannot be delivered because the user is offline."), TTI_ERROR);
				resetText = false;
			}
		} else if(Util::stricmp(cmd.c_str(), _T("outbox")) == 0) {
			MessageOutboxDlg(this).run();
		} else if(Util::stricmp(cmd.c_str(), _T("grant")) == 0) {
			handleGrantSlot();
			addStatus(T_("Slot granted"));
		} else if(Util::stricmp(cmd.c_str(), _T("close")) == 0) {
			postMessage(WM_CLOSE);
		} else if(Util::stricmp(cmd.c_str(), _T("direct")) == 0 || Util::stricmp(cmd.c_str(), _T("encrypted")) == 0) {
			startCC();
		} else if((Util::stricmp(cmd.c_str(), _T("favorite")) == 0) || (Util::stricmp(cmd.c_str(), _T("fav")) == 0)) {
			handleAddFavorite();
			addStatus(T_("Favorite user added"));
		} else if(Util::stricmp(cmd.c_str(), _T("getlist")) == 0) {
			handleGetList(getParent());
		} else if(Util::stricmp(cmd.c_str(), _T("ignore")) == 0) {
			handleIgnoreChat(true);
		} else if(Util::stricmp(cmd.c_str(), _T("unignore")) == 0) {
			handleIgnoreChat(false);
		} else if(Util::stricmp(cmd.c_str(), _T("log")) == 0) {
			openLog();
		} else if(Util::stricmp(cmd.c_str(), _T("lastmessage")) == 0) {
			addStatus(Text::toT(str(F_("Last message occured %1%") % Util::getTimeString(lastMessageTime, "%c"))));
		} else if(Util::stricmp(cmd.c_str(), _T("help")) == 0) {
			bool bShowBriefCommands = !param.empty() && (Util::stricmp(param.c_str(), _T("brief")) == 0);

			if(bShowBriefCommands)
			{
				addChat(T_("*** Keyboard commands:") + _T("\r\n") +
						ChatCommands::commands +
						_T(", /direct, /encrypted, /outbox, /rtf <message>, /getlist, /grant, /close, /favorite, /ignore, /unignore, /log <system, downloads, uploads>, /lastmessage")
						);
			}
			else
			{
				addChat(T_("*** Keyboard commands:") + _T("\r\n") +
						ChatCommands::getDescriptiveCommands() +
						+ _T("\r\n") _T("/direct")
						+ _T("\r\n") _T("/encrypted")
						+ _T("\r\n\t") + T_("Starts a direct encrypted communication channel to avoid spying on your private messages.")
						+ _T("\r\n") _T("/rtf <message>")
						+ _T("\r\n\t") + T_("Sends a CommonMark ADC RTF0 message only when the active route and recipient support it. Unsupported messages are left unsent. Attachments and inline media must use an ADC magnet URI containing a TTH and exact size, and the matching file must be available in the share exposed to that hub.")
						+ _T("\r\n") _T("/getlist")
						+ _T("\r\n\t") + T_("Adds the current user's list to the Download Queue.")
						+ _T("\r\n") _T("/grant")
						+ _T("\r\n\t") + T_("Grants the remote user a slot. Once they connect, or if they don't connect in 10 minutes, the granted slot is removed.")
						+ _T("\r\n") _T("/favorite")
						+ _T("\r\n") _T("/fav")
						+ _T("\r\n\t") + T_("Adds the current user to the list of Favorite Users.")
						+ _T("\r\n") _T("/ignore")
						+ _T("\r\n\t") + T_("Adds a user matching definition (or modifies an existing one, if possible) to ignore chat messages from the current user.")
						+ _T("\r\n") _T("/unignore")
						+ _T("\r\n\t") + T_("Adds a user matching definition (or modifies an existing one, if possible) to stop ignoring chat messages from the current user.")
						+ _T("\r\n") _T("/lastmessage")
						+ _T("\r\n\t") + T_("Lists the date and time when the last message was sent or received.")
						);
			}

		} else if(SETTING(SEND_UNKNOWN_COMMANDS)) {
			send = true;
		} else {
			addStatus(str(TF_("Unknown command: %1%") % cmd));
		}

	} else {
		send = true;
	}

	if(send) {
		if(online || ccReady() || MessageOutboxManager::getInstance()->isEnabled()) {
			if(!sendMessage(s, false, false, true)) {
				message->showPopup(T_("Message not sent"),
					T_("The message could not be sent or queued. It is still in the input box."), TTI_ERROR);
				resetText = false;
			}
		} else {
			message->showPopup(T_("User offline"), T_("Enable offline messages in View > Message outbox to save this message for later."), TTI_ERROR);
			resetText = false;
		}
	}
	if(resetText) {
		message->setText(Util::emptyStringT);
	}
}

bool PrivateFrame::queueMessage(const string& text, bool thirdPerson, bool direct) {
	try {
		auto id = MessageOutboxManager::getInstance()->enqueue(replyTo.getUser(), text, thirdPerson, direct);
		addStatus(T_("Message queued locally (#") + Text::toT(Util::toString(id)) +
			(direct ? T_("). Waiting for a direct encrypted channel; manage it in View > Message outbox.") :
			T_("). Waiting for the original hub route; manage it in View > Message outbox.")));
		return true;
	} catch(const std::exception& e) {
		addStatus(T_("Message was not queued: ") + Text::toT(e.what()));
		return false;
	}
}

bool PrivateFrame::sendMessage(const tstring& msg, bool thirdPerson, bool explicitRichText, bool allowQueue) {
	auto msg8 = Text::fromT(msg);
	if(explicitRichText && !SETTING(ENABLE_RICH_TEXT)) {
		return false;
	}
	auto outbox = MessageOutboxManager::getInstance();
	const bool canQueue = allowQueue && !explicitRichText && outbox->isEnabled();
	// Automatically attempting CCPM is a preference, not an instruction to
	// queue an otherwise deliverable online message until a handshake succeeds.
	const bool direct = !replyTo.getUser().user->isNMDC() &&
		(ccReady() || (!online && (outboxRequireDirect.load() || SETTING(ALWAYS_CCPM))));
	if(canQueue) {
		try {
			// Preserve ordering if there are already messages awaiting this route.
			if((!online && !ccReady()) || (direct && !ccReady()) ||
				outbox->getStore().hasPending(replyTo.getUser().user->getCID().toBase32(), replyTo.getUser().hint))
				return queueMessage(msg8, thirdPerson, direct);
		} catch(const std::exception& e) {
			addStatus(T_("Outbox unavailable: ") + Text::toT(e.what()));
			return false;
		}
	}

	{
		Lock lifetimeLock(connLifetimeMutex);
		UserConnection* activeConn;
		{
			Lock l(mutex);
			activeConn = conn.load();
		}
		if(activeConn && activeConn->getState() == UserConnection::STATE_CMD && activeConn->isSecure()) {
			if(explicitRichText && !activeConn->supportsRTF0()) {
				return false;
			}
			return activeConn->pm(msg8, thirdPerson, explicitRichText);
		}
	}

	// The direct channel can disappear between the first check and the send.
	// Queued direct messages must never fall back to a hub in that race.
	if(canQueue && direct) return queueMessage(msg8, thirdPerson, true);
	if(explicitRichText && !ClientManager::getInstance()->supportsRichText(replyTo.getUser())) {
		return false;
	}
	if(explicitRichText) {
		auto checked = msg8;
		if(!RichText::prepareOutgoingMessage(checked, true, replyTo.getUser().hint)) return false;
	}
	const auto sent = ClientManager::getInstance()->privateMessage(replyTo.getUser(), msg8, thirdPerson,
		replyTo.getUser().user->isNMDC(), explicitRichText);
	return sent || (canQueue && queueMessage(msg8, thirdPerson, direct));
}

PrivateFrame::UserInfoList PrivateFrame::selectedUsersImpl() {
	return UserInfoList(1, &replyTo);
}

void PrivateFrame::tabMenuImpl(dwt::Menu* menu) {
	menu->appendItem(T_("Message outbox..."), [this] { MessageOutboxDlg(this).run(); });
	appendUserItems(getParent(), menu, false, false);
	prepareMenu(menu, UserCommand::CONTEXT_USER, replyTo.getUser().hint);
	menu->appendSeparator();
}

bool PrivateFrame::handleChatContextMenu(dwt::ScreenCoordinate pt) {
	if(pt.x() == -1 || pt.y() == -1) {
		pt = chat->getContextMenuPos();
	}

	tstring searchText;
	ChatText::getChatSelText(chat, searchText, pt);

	auto menu = chat->getMenu(searchText);
	if(SETTING(ENABLE_PRIVATE_SEND) && DirectTransferManager::available()) {
		menu->appendItem(T_("Private Send..."), [this] { handlePrivateSend(); }, IconManager::menuIcon(IDI_FILE));
		menu->appendSeparator();
	}

	LinkActions::addSearchMenu(menu.get(), searchText);

	menu->setTitle(escapeMenu(getText()), getParent()->getIcon(this));

	prepareMenu(menu.get(), UserCommand::CONTEXT_USER, replyTo.getUser().hint);

	menu->open(pt);
	return true;
}

void PrivateFrame::handleChannelMenu() {
	auto menu = addChild(ControlSeeds::menu);
	menu->appendItem(T_("Message outbox..."), [this] { MessageOutboxDlg(this).run(); });
	menu->appendSeparator();
	if(SETTING(ENABLE_PRIVATE_SEND) && DirectTransferManager::available()) {
		menu->appendItem(T_("Private Send..."), [this] { handlePrivateSend(); }, IconManager::menuIcon(IDI_FILE));
		menu->appendSeparator();
	}

	menu->setTitle(T_("Communication channel"));

	auto hubs = ClientManager::getInstance()->getHubs(replyTo.getUser());
	const auto cc = ccReady();

	if(hubs.empty()) {
		menu->appendItem(T_("(User offline)"), nullptr, nullptr, false);

	} else {
		for(auto& hub: hubs) {
			auto url = hub.first;
			auto current = !cc && hubHintsEqual(url, replyTo.getUser().hint);
			auto pos = menu->appendItem(dwt::util::escapeMenu(Text::toT(hub.second)),
				[this, url] { changeHub(url); }, nullptr, !current);
			if(current) {
				menu->checkItem(pos);
			}
		}

	}
	// A direct channel remains usable (and must remain disconnectable) when
	// its hub is offline, or when CCPM has subsequently been disabled.
	if(cc || (SETTING(ENABLE_CCPM) && !hubs.empty())) {
		menu->appendSeparator();
		if(cc) {
			menu->appendItem(T_("Disconnect the direct encrypted channel"), [this] { closeCC(); });
		} else {
			menu->appendItem(T_("Start a direct encrypted channel"), [this] { startCC(); }, IconManager::menuIcon(IDI_SECURE));
			if(!ccpmAttemptToken.empty() || (allowAutoCCPM && nextAutoCCPMAttempt > GET_TICK()))
				menu->appendItem(T_("Cancel direct channel connection"), [this] { closeCC(); });
		}
	}

	menu->open();
}

void PrivateFrame::runUserCommand(const UserCommand& uc) {
	if(!UserCommands::getUCParams(this, uc, ucLineParams))
		return;

	auto ucParams = ucLineParams;
	ClientManager::getInstance()->userCommand(replyTo.getUser(), uc, ucParams, true);
}

void PrivateFrame::on(ClientManagerListener::UserConnected, const UserPtr& aUser) noexcept {
	if(replyTo.getUser() == aUser)
		callAsync([this] { updateOnlineStatus(); });
}

void PrivateFrame::on(ClientManagerListener::UserUpdated, const OnlineUser& aUser) noexcept {
	if(replyTo.getUser() == aUser.getUser())
		callAsync([this] { updateOnlineStatus(); });
}

void PrivateFrame::on(ClientManagerListener::UserDisconnected, const UserPtr& aUser) noexcept {
	if(replyTo.getUser() == aUser)
		callAsync([this] { updateOnlineStatus(); });
}

void PrivateFrame::on(ConnectionManagerListener::Connected, ConnectionQueueItem* cqi, UserConnection* uc) noexcept {
	if(cqi->getType() != CONNECTION_TYPE_PM || cqi->getUser().user != replyTo.getUser().user) {
		return;
	}

	if(!uc->isSecure()) {
		uc->disconnect(true);
		return;
	}

	const auto token = cqi->getToken();
	const auto connectedHint = cqi->getUser().hint;
	uint64_t revision = 0;
	UserConnection* oldConn = nullptr;
	bool reject = false;
	bool alreadyCurrent = false;
	{
		Lock lifetimeLock(connLifetimeMutex);
		{
			Lock l(mutex);
			if(!acceptCCPMConnections || !matchesCurrentHub(cqi->getUser(), replyTo.getUser())) {
				reject = true;
			} else {
				oldConn = conn.load();
				if(oldConn == uc && connToken == token) {
					alreadyCurrent = true;
				} else {
					conn.store(uc);
					connToken = token;
					connEstablishedTick = GET_TICK();
					outboxRequireDirect = true;
					revision = ++connRevision;
				}
			}
		}

		if(!alreadyCurrent && oldConn && oldConn != uc) {
			oldConn->disconnect(false);
		}
		if(!reject) {
			// Keep adoption and fallback release atomic with tab closing.
			// Otherwise closing could re-park this channel in between and this
			// callback would subsequently remove its final owner.
			PrivateChatManager::getInstance()->releasePMConn(
				replyTo.getUser().user, token, false);
		}
	}
	if(reject) {
		// PrivateChatManager leaves connections for an open frame to adopt.
		// Reject one that raced with closing or a hub change so it isn't left
		// alive without an owner.
		uc->disconnect(true);
		return;
	}

	if(alreadyCurrent) {
		return;
	}

	callAsync([this, token, connectedHint, revision] {
		bool current = false;
		{
			Lock l(mutex);
			if(conn.load() && connToken == token && connRevision.load() == revision) {
				current = true;
				if(replyTo.getUser().hint.empty()) {
					replyTo.getUser().hint = connectedHint;
				}
			}
		}
		if(current) {
			cancelCCPMAttempt();
			cancelAutoCCPMReconnect();
			nextAutoCCPMAttempt = 0;
			localTyping = false;
			remoteTyping = false;
			updateOnlineStatus(true);
			addStatus(T_("A direct encrypted channel has been established"));
			updateTypingState();
			sendSeenIfActive();
		}
	});
}

void PrivateFrame::on(ConnectionManagerListener::Removed, ConnectionQueueItem* cqi) noexcept {
	if(cqi->getType() != CONNECTION_TYPE_PM || cqi->getUser().user != replyTo.getUser().user) {
		return;
	}

	const auto token = cqi->getToken();
	uint64_t revision = 0;
	uint64_t establishedTick = 0;
	bool removedCurrent = false;
	{
		Lock lifetimeLock(connLifetimeMutex);
		{
			Lock l(mutex);
			if(conn.load() && connToken == token) {
				conn.store(nullptr);
				connToken.clear();
				establishedTick = connEstablishedTick;
				connEstablishedTick = 0;
				revision = ++connRevision;
				removedCurrent = true;
			}
		}
	}

	// This is deliberately token-specific. PrivateChatManager may have handled
	// this event just before a closing frame tried to return the same connection.
	PrivateChatManager::getInstance()->releasePMConn(replyTo.getUser().user, token, false);
	if(!removedCurrent) {
		return;
	}

	const auto error = Text::toT(cqi->getLastError());
	callAsync([this, revision, establishedTick, error] {
		if(isDisconnectedConnection(revision)) {
			const auto now = GET_TICK();
			if(establishedTick != 0 && now - establishedTick >= getCCPMStableConnectionTime()) {
				autoCCPMAttempts = 0;
			}
			nextAutoCCPMAttempt = now + getCCPMReconnectDelay(autoCCPMAttempts);
			updateTypingState(false);
			remoteTyping = false;
			addStatus(error.empty() ? T_("The direct encrypted channel has been disconnected") :
				str(TF_("The direct encrypted channel has been disconnected: %1%") % error));
			updateOnlineStatus(true);
		}
	});
}
