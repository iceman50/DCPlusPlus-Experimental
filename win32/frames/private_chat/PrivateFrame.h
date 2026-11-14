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

#ifndef DCPLUSPLUS_WIN32_PRIVATE_FRAME_H
#define DCPLUSPLUS_WIN32_PRIVATE_FRAME_H

#include <atomic>
#include <dcpp/user/HintedUser.h>
#include <unordered_map>
#include <dcpp/util/Util.h>

#include <dcpp/hub/ClientManagerListener.h>
#include <dcpp/connection/ConnectionManagerListener.h>
#include <dcpp/core/thread/CriticalSection.h>
#include <dcpp/user/User.h>

#include <win32/frames/MDIChildFrame.h>
#include <win32/frames/IRecent.h>
#include <win32/AspectChat.h>
#include <win32/UserInfoBase.h>
#include <win32/AspectUserCommand.h>

class PrivateFrame :
	public MDIChildFrame<PrivateFrame>,
	public IRecent<PrivateFrame>,
	private ClientManagerListener,
	private ConnectionManagerListener,
	public AspectChat<PrivateFrame>,
	public AspectUserInfo<PrivateFrame>,
	public AspectUserCommand<PrivateFrame>
{
	typedef MDIChildFrame<PrivateFrame> BaseType;
	typedef AspectChat<PrivateFrame> ChatType;

	friend class MDIChildFrame<PrivateFrame>;
	friend class AspectChat<PrivateFrame>;
	friend class AspectUserInfo<PrivateFrame>;
	friend class AspectUserCommand<PrivateFrame>;

	using IRecent<PrivateFrame>::setText;

public:
	enum Status {
		STATUS_STATUS,
		STATUS_PRIVATE_SEND,
		STATUS_CHANNEL,
		STATUS_LAST
	};

	enum PMInfo {
		PM_INFO_SEEN,
		PM_INFO_TYPING_ON,
		PM_INFO_TYPING_OFF,
		PM_INFO_NO_AUTOCONNECT,
		PM_INFO_QUIT
	};

	static const string id;
	const string& getId() const;

	/// @return whether a new window can be opened (wrt the "Max PM windows" setting).
	static bool gotMessage(TabViewPtr parent, const ChatMessage& message, const string& hubHint, bool fromBot);
	static void openWindow(TabViewPtr parent, const HintedUser& replyTo, const tstring& msg = Util::emptyStringT,
		const string& logPath = Util::emptyString, bool activate = true);
	static void activateWindow(const UserPtr& u);
	static bool isOpen(const UserPtr& u);
	static bool privateSendNotice(TabViewPtr parent, const HintedUser& peer, const tstring& text);
	static void handlePMConnection(const HintedUser& user, const string& connectionToken);
	static void handlePMMessage(const HintedUser& user, const string& connectionToken,
		const ChatMessage& message);
	static void handlePMI(const HintedUser& user, const string& connectionToken, const AdcCommand& cmd);
	static void closeAll(bool offline);
	static void refreshRichTextSettings();

	WindowParams getWindowParams() const;
	static void parseWindowParams(TabViewPtr parent, const WindowParams& params);
	static bool isFavorite(const WindowParams& params);

	/** Returns true after sending, or durably queuing an eligible typed message.
	 * Automatic messages and explicit RTF0 messages are never queued. */
	bool sendMessage(const tstring& msg, bool thirdPerson = false, bool explicitRichText = false, bool allowQueue = false);

private:
	enum {
		TIMER_CPMI_TYPING = 1,
		TIMER_CCPM_RECONNECT,
		TIMER_PRIVATE_SEND,
		TIMER_CCPM_CONNECT
	};

	UserInfoBase replyTo;
	bool online;

	// ConnectionManager::Removed takes this lock before returning to the
	// UserConnection deletion path. Holding it therefore pins conn while it is
	// being used.
	mutable CriticalSection connLifetimeMutex;
	mutable CriticalSection mutex;
	std::atomic<UserConnection*> conn;
	string connToken;
	std::atomic<uint64_t> connRevision;
	uint64_t connEstablishedTick;
	std::atomic_bool outboxRequireDirect { false };
	bool queueMessage(const string& text, bool thirdPerson, bool direct);
	bool acceptCCPMConnections;
	bool localTyping;
	bool remoteTyping;
	bool messageSeenPending;
	bool allowAutoCCPM;
	uint64_t nextAutoCCPMAttempt;
	unsigned autoCCPMAttempts;
	bool autoCCPMReconnectScheduled;
	string ccpmAttemptToken;
	bool privateSendReady = false;
	tstring lastStatus;

	time_t lastMessageTime;

	ParamMap ucLineParams;

	typedef unordered_map<UserPtr, PrivateFrame*, User::Hash> FrameMap;
	// Frame creation, lookup-and-use and destruction remain on the UI thread;
	// this mutex also makes the worker-thread isOpen probe data-race free.
	static FrameMap frames;
	static CriticalSection framesMutex;

	PrivateFrame(TabViewPtr parent, const HintedUser& replyTo_, const string& logPath = Util::emptyString);
	virtual ~PrivateFrame();

	void layout();
	bool preClosing();

	string getLogPath() const;
	void openLog();
	void fillLogParams(ParamMap& params) const;
	void addedChat(const tstring& message);
	void addStatus(const tstring& text);
	void updatePMInfo(PMInfo type);
	void updateTypingState(bool refreshTimeout = true);
	void sendSeenIfActive();
	bool sendPMI(const char* name, const string& value);
	void updateOnlineStatus(bool newChannel = false);
	void updateChannel();
	void updateRichTextAvailability();
	void updatePrivateSendAvailability();
	void handlePrivateSend();
	void startCC(bool silent = false);
	void cancelCCPMAttempt();
	void failCCPMAttempt(const string& token, const tstring& reason);
	void closeCC(bool silent = false);
	void cancelAutoCCPMReconnect();
	void scheduleAutoCCPMReconnect();
	void pauseAutoCCPMReconnect();
	void changeHub(const string& hubHint);
	void adoptPMConnection(const string& connectionToken);
	bool ccReady() const;
	bool isCurrentConnection(const string& token, uint64_t revision) const;
	bool isDisconnectedConnection(uint64_t revision) const;

	bool handleChatContextMenu(dwt::ScreenCoordinate pt);
	void handleChannelMenu();

	void runUserCommand(const UserCommand& uc);

	// MDIChildFrame
	void tabMenuImpl(dwt::Menu* menu);

	// AspectChat
	void enterImpl(const tstring& s);

	// AspectUserInfo
	UserInfoList selectedUsersImpl();

	// ClientManagerListener
	virtual void on(ClientManagerListener::UserConnected, const UserPtr& aUser) noexcept;
	virtual void on(ClientManagerListener::UserUpdated, const OnlineUser& aUser) noexcept;
	virtual void on(ClientManagerListener::UserDisconnected, const UserPtr& aUser) noexcept;

	// ConnectionManagerListener
	virtual void on(ConnectionManagerListener::Connected, ConnectionQueueItem* cqi, UserConnection* uc) noexcept;
	virtual void on(ConnectionManagerListener::Removed, ConnectionQueueItem* cqi) noexcept;
	virtual void on(ConnectionManagerListener::Failed, ConnectionQueueItem* cqi, const string& error) noexcept;
};

#endif // !defined(PRIVATE_FRAME_H)
