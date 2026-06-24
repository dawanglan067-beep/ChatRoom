#pragma once

#include <QString>

namespace UiText
{
namespace AuthDialog
{
inline const QString kBackendUrlSaved = QStringLiteral("后端地址已保存。");
inline const QString kRequestingCode = QStringLiteral("正在发送验证码...");
inline const QString kVerifyingCode = QStringLiteral("正在验证验证码...");
inline const QString kWindowTitle = QStringLiteral("登录");
inline const QString kTitle = QStringLiteral("邮箱验证码登录");
inline const QString kDescription =
    QStringLiteral("客户端通过后端认证接口登录。若 MAIL_MODE=log，验证码会直接显示在这里并打印到后端日志中。");
inline const QString kBackendUrlPlaceholder = QStringLiteral("例如：http://127.0.0.1:3000");
inline const QString kEmailPlaceholder = QStringLiteral("请输入邮箱");
inline const QString kCodePlaceholder = QStringLiteral("请输入6位验证码");
inline const QString kBackendUrlLabel = QStringLiteral("后端地址：");
inline const QString kEmailLabel = QStringLiteral("邮箱：");
inline const QString kCodeLabel = QStringLiteral("验证码：");
inline const QString kSaveButton = QStringLiteral("保存");
inline const QString kSendCodeButton = QStringLiteral("发送验证码");
inline const QString kVerifyEnterButton = QStringLiteral("验证并进入");
inline const QString kInitialStatus = QStringLiteral("请先启动后端，再发送验证码。");
}

namespace AuthService
{
inline const QString kCodeSentFallback = QStringLiteral("验证码已发送，请查看后端输出。");
inline const QString kSignInSuccessFallback = QStringLiteral("登录成功。");
inline const QString kInvalidEmail = QStringLiteral("请输入有效的邮箱地址。");
inline const QString kInvalidCode = QStringLiteral("请输入6位数字验证码。");
inline const QString kInvalidBackendUrl =
    QStringLiteral("请输入有效的后端地址，例如：http://127.0.0.1:3000");
inline const QString kBackendTimeout = QStringLiteral("后端请求超时，请检查服务是否已启动。");
inline const QString kRequestFailedPattern = QStringLiteral("请求失败：%1");
inline const QString kBackendHttpErrorPattern = QStringLiteral("后端返回 HTTP %1");
}

namespace ChatClient
{
inline const QString kConnected = QStringLiteral("已连接");
inline const QString kDisconnected = QStringLiteral("已断开");
inline const QString kInvalidJson = QStringLiteral("收到无效的 JSON 数据。");
inline const QString kUnknownSocketError = QStringLiteral("未知 WebSocket 错误");
inline const QString kErrorPattern = QStringLiteral("错误：%1");
inline const QString kIdle = QStringLiteral("空闲");
inline const QString kWebSocketsUnavailable = QStringLiteral("当前 Qt 未启用 WebSockets 模块。");
inline const QString kSocketNotInitialized = QStringLiteral("WebSocket 客户端未初始化。");
inline const QString kConnectingPattern = QStringLiteral("正在连接 %1");
inline const QString kSocketNotConnected = QStringLiteral("WebSocket 未连接。");
inline const QString kQueuedReconnecting = QStringLiteral("已离线，消息已排队，正在重连...");
inline const QString kReconnectingInPattern = QStringLiteral("%1 秒后重连...");
}

namespace MessageBubble
{
inline const QString kSelfAvatar = QStringLiteral("我");
inline const QString kFallbackAvatar = QStringLiteral("聊");
inline const QString kSystemSenderKey = QStringLiteral("system");
inline const QString kSystemPrefix = QStringLiteral("[系统]");
inline const QString kSending = QStringLiteral("发送中");
inline const QString kQueued = QStringLiteral("排队中");
inline const QString kDelivered = QStringLiteral("已送达");
inline const QString kRead = QStringLiteral("已读");
inline const QString kFailed = QStringLiteral("发送失败");

// Media display
inline const QString kImageTag = QStringLiteral("图片");
inline const QString kFileTag = QStringLiteral("文件");
inline const QString kImageLoadingPlaceholder = QStringLiteral("\U0001f5bc \u56fe\u7247\u52a0\u8f7d\u4e2d...");
inline const QString kClickToOpen = QStringLiteral("\u70b9\u51fb\u6253\u5f00");
inline const QString kClickToOpenWithSizePattern = QStringLiteral("%1 \u00b7 \u70b9\u51fb\u6253\u5f00");
}

namespace MainWindow
{
inline const QString kSenderMessagePattern = QStringLiteral("%1：%2");
inline const QString kStatusMessageSendFailed = QStringLiteral("状态：消息发送失败");
inline const QString kStatusMessageSendFailedDetail =
    QStringLiteral("后端未确认消息，双击失败消息可重试。");
inline const QString kStatusConnectRealtimeFirst = QStringLiteral("状态：请先连接实时通道");
inline const QString kStatusInvalidAddress = QStringLiteral("状态：地址无效");
inline const QString kInvalidWebSocketUrl = QStringLiteral("WebSocket 地址无效");
inline const QString kStatusSignInRequired = QStringLiteral("状态：请先登录");
inline const QString kDialogProfile = QStringLiteral("账号资料");
inline const QString kProfileEmailLabel = QStringLiteral("邮箱：");
inline const QString kProfileNicknameLabel = QStringLiteral("昵称：");
inline const QString kProfileAvatarLabel = QStringLiteral("头像：");
inline const QString kProfileNicknamePlaceholder = QStringLiteral("请输入昵称");
inline const QString kProfileAvatarPlaceholder = QStringLiteral("可选：选择本地图片，或填写 http/https 图片链接");
inline const QString kProfileChooseAvatarButton = QStringLiteral("更换头像");
inline const QString kProfileRemoveAvatarButton = QStringLiteral("移除头像");
inline const QString kStatusProfileUpdateFailed = QStringLiteral("状态：资料更新失败");
inline const QString kStatusProfileUpdated = QStringLiteral("状态：资料已更新");
inline const QString kDialogNewDirect = QStringLiteral("新建单聊");
inline const QString kDialogNewDirectPrompt = QStringLiteral("请输入对方已注册邮箱：");
inline const QString kStatusCreateDirectFailed = QStringLiteral("状态：创建单聊失败");
inline const QString kDialogCreateConversationFailed = QStringLiteral("创建会话失败");
inline const QString kStatusDirectCreated = QStringLiteral("状态：已创建单聊");
inline const QString kDialogNewGroup = QStringLiteral("新建群聊");
inline const QString kDialogGroupNamePrompt = QStringLiteral("请输入群名称：");
inline const QString kDialogMembersPrompt =
    QStringLiteral("请输入成员邮箱（逗号、分号或换行分隔）：");
inline const QString kStatusMemberEmailRequired = QStringLiteral("状态：至少需要一个成员邮箱");
inline const QString kStatusCreateGroupFailed = QStringLiteral("状态：创建群聊失败");
inline const QString kDialogCreateGroupFailed = QStringLiteral("创建群聊失败");
inline const QString kStatusGroupCreated = QStringLiteral("状态：群聊已创建");
inline const QString kStatusNotGroup = QStringLiteral("状态：当前会话不是群聊");
inline const QString kDialogInviteMembers = QStringLiteral("邀请成员");
inline const QString kStatusInviteFailed = QStringLiteral("状态：邀请成员失败");
inline const QString kDialogInviteFailed = QStringLiteral("邀请成员失败");
inline const QString kStatusMembersInvited = QStringLiteral("状态：成员邀请成功");
inline const QString kAddedMembersPattern = QStringLiteral("已新增 %1 位成员。");
inline const QString kDialogRemoveMember = QStringLiteral("移除成员");
inline const QString kDialogRemoveMemberPrompt = QStringLiteral("请输入要移除的成员邮箱：");
inline const QString kDialogRemoveMemberFailed = QStringLiteral("移除成员失败");
inline const QString kDialogGroupMembers = QStringLiteral("群成员");
inline const QString kDialogMembersOfPattern = QStringLiteral("%1 的成员");
inline const QString kMemberDisplayPattern = QStringLiteral("%1（%2）");
inline const QString kOwnerTag = QStringLiteral(" [群主]");
inline const QString kSelfTag = QStringLiteral(" [你]");
inline const QString kRemoveSelected = QStringLiteral("移除所选");
inline const QString kRemoveConfirmPattern = QStringLiteral("确认将 %1 移出该群？");
inline const QString kDialogLeaveGroup = QStringLiteral("退出群聊");
inline const QString kLeaveGroupConfirm = QStringLiteral("确认退出当前群聊？");
inline const QString kStatusLeaveGroupFailed = QStringLiteral("状态：退出群聊失败");
inline const QString kDialogLeaveGroupFailed = QStringLiteral("退出群聊失败");
inline const QString kStatusLeftGroup = QStringLiteral("状态：已退出群聊");
inline const QString kStatusRetryFailed = QStringLiteral("状态：重试失败");
inline const QString kStatusRealtimeNotConnected = QStringLiteral("实时通道未连接。");
inline const QString kStatusMessageQueuedDetail =
    QStringLiteral("消息已排队，重连成功后会自动发送。");
inline const QString kStatusRetryQueuedDetail =
    QStringLiteral("重试消息已排队，重连成功后会自动发送。");
inline const QString kQueuedMessagesPattern = QStringLiteral("当前排队消息：%1");
inline const QString kQueuedMessagesInlinePattern = QStringLiteral("（排队 %1）");
inline const QString kNoConversation = QStringLiteral("暂无会话");
inline const QString kSignInFirst = QStringLiteral("请先完成后端登录。");
inline const QString kRoomLoadedPattern = QStringLiteral("会话 %1 | 已加载 %2 条消息");
inline const QString kAccountRoomLoadedPattern = QStringLiteral("账号：%1 | 会话 %2 | 已加载 %3 条消息");
inline const QString kStatusPattern = QStringLiteral("状态：%1");
inline const QString kDisconnect = QStringLiteral("断开连接");
inline const QString kConnect = QStringLiteral("连接");
inline const QString kStatusRealtimeMessage = QStringLiteral("状态：收到实时消息");
inline const QString kStatusRealtimeConnected = QStringLiteral("状态：实时通道已连接");
inline const QString kStatusJoinedConversation = QStringLiteral("状态：已加入当前会话");
inline const QString kStatusServerError = QStringLiteral("状态：服务器错误");
inline const QString kStatusRealtimeEvent = QStringLiteral("状态：收到实时事件");
inline const QString kWindowTitle = QStringLiteral("ChatRoom");
inline const QString kSidebarTitle = QStringLiteral("会话");
inline const QString kNewDirectButton = QStringLiteral("新建单聊");
inline const QString kNewGroupButton = QStringLiteral("新建群聊");
inline const QString kSidebarHint =
    QStringLiteral("会话列表和历史消息来自后端，实时消息来自 WebSocket。");
inline const QString kMembersButton = QStringLiteral("成员");
inline const QString kInviteButton = QStringLiteral("邀请");
inline const QString kRemoveMemberButton = QStringLiteral("移除成员");
inline const QString kLeaveButton = QStringLiteral("退出");
inline const QString kQueueDetailsButton = QStringLiteral("排队详情");
inline const QString kQueueDetailsButtonWithCountPattern = QStringLiteral("排队详情(%1)");
inline const QString kQueueDetailsDialogTitle = QStringLiteral("排队消息详情");
inline const QString kNoQueuedMessages = QStringLiteral("当前没有排队消息。");
inline const QString kQueuedMessageItemPattern = QStringLiteral("[%1] %2\n%3");
inline const QString kMessageInputPlaceholder =
    QStringLiteral("在这里发送的消息会由后端持久化并通过 WebSocket 广播。");
inline const QString kSendButton = QStringLiteral("发送");
inline const QString kStatusConnectionError = QStringLiteral("状态：连接错误");
inline const QString kStatusMissingSession = QStringLiteral("状态：缺少登录会话");
inline const QString kStatusLoadConversationsFailed = QStringLiteral("状态：加载会话失败");
inline const QString kStatusConversationsLoaded = QStringLiteral("状态：会话已加载");
inline const QString kLoadedConversationsPattern = QStringLiteral("已加载 %1 个会话。");
inline const QString kStatusLoadMessagesFailed = QStringLiteral("状态：加载消息失败");
inline const QString kStatusHistoryLoaded = QStringLiteral("状态：历史消息已加载");
inline const QString kLoadedMessagesPattern = QStringLiteral("已加载 %1 条消息。");
inline const QString kStatusLoadingOlderMessages = QStringLiteral("状态：正在加载更早消息");
inline const QString kStatusLoadOlderMessagesFailed = QStringLiteral("状态：加载更早消息失败");
inline const QString kStatusNoOlderMessages = QStringLiteral("状态：已无更早消息");
inline const QString kStatusOlderMessagesLoaded = QStringLiteral("状态：更早消息加载完成");
inline const QString kOlderMessagesReachedBeginning = QStringLiteral("已到最早一条");
inline const QString kLoadedOlderMessagesPattern = QStringLiteral("新增 %1 条");
inline const QString kUnknownTime = QStringLiteral("--");
inline const QString kUnknownLastSeen = QStringLiteral("未知");
inline const QString kMembersLoadFailed = QStringLiteral("成员：加载失败");
inline const QString kUnknownOwner = QStringLiteral("未知");
inline const QString kOwnerMembersNonePattern = QStringLiteral("群主：%1 | 成员：无");
inline const QString kOwnerMembersPattern = QStringLiteral("群主：%1 | 成员：%2");
inline const QString kMembersTotalPattern = QStringLiteral("（共 %1 人）");
inline const QString kBackendTimeoutShort = QStringLiteral("后端请求超时。");
inline const QString kHttpErrorPattern = QStringLiteral("HTTP %1");
inline const QString kLobby = QStringLiteral("大厅");
inline const QString kNotGroupError = QStringLiteral("当前会话不是群聊。");
inline const QString kStatusRemoveMemberFailed = QStringLiteral("状态：移除成员失败");
inline const QString kStatusMemberRemoved = QStringLiteral("状态：成员已移除");
inline const QString kStatusSelectConversationFirst = QStringLiteral("状态：请先选择会话");
inline const QString kStatusUploadingFile = QStringLiteral("状态：正在上传文件");
inline const QString kStatusSendFileFailed = QStringLiteral("状态：发送文件失败");
inline const QString kStatusFileSent = QStringLiteral("状态：文件已发送");
inline const QString kStatusLoadFriendsFailed = QStringLiteral("状态：加载好友失败");
inline const QString kStatusSendFriendRequestFailed = QStringLiteral("状态：好友申请失败");
inline const QString kStatusAlreadyFriends = QStringLiteral("状态：你们已经是好友");
inline const QString kStatusFriendsAutoAccepted = QStringLiteral("状态：已自动互相通过好友");
inline const QString kStatusFriendRequestSent = QStringLiteral("状态：好友申请已发送");
inline const QString kStatusLoadFriendRequestsFailed = QStringLiteral("状态：加载好友申请失败");
inline const QString kStatusHandleFriendRequestFailed = QStringLiteral("状态：处理好友申请失败");
inline const QString kStatusFriendRequestAccepted = QStringLiteral("状态：已同意好友申请");
inline const QString kStatusFriendRequestRejected = QStringLiteral("状态：已拒绝好友申请");
inline const QString kStatusLoadBlacklistFailed = QStringLiteral("状态：加载黑名单失败");
inline const QString kStatusBlockFailed = QStringLiteral("状态：拉黑失败");
inline const QString kStatusBlockedUser = QStringLiteral("状态：已拉黑用户");
inline const QString kStatusUnblockFailed = QStringLiteral("状态：解除拉黑失败");
inline const QString kStatusUnblockedUser = QStringLiteral("状态：已解除拉黑");
inline const QString kStatusGlobalSearchFailed = QStringLiteral("状态：全局搜索失败");
inline const QString kStatusLocateMessageFailed = QStringLiteral("状态：定位消息失败");
inline const QString kStatusFavoriteRemoved = QStringLiteral("状态：已取消收藏");
inline const QString kStatusFavorited = QStringLiteral("状态：已收藏消息");
inline const QString kStatusRecallFailed = QStringLiteral("状态：撤回失败");
inline const QString kStatusRecallRequested = QStringLiteral("状态：已请求撤回");
inline const QString kStatusOpenFileFailed = QStringLiteral("状态：打开文件失败");
inline const QString kStatusFileOpened = QStringLiteral("状态：已打开文件");
inline const QString kStatusResendQueueTriggered = QStringLiteral("状态：已触发排队消息重发");
inline const QString kStatusMessageRecalled = QStringLiteral("状态：消息已撤回");
inline const QString kNoResendableQueuedMessages = QStringLiteral("没有可重发的排队消息。");
inline const QString kSelectQueuedMessageFirst = QStringLiteral("请先选中一条排队消息。");
inline const QString kRemoveQueuedMessageFailed = QStringLiteral("删除失败：该消息可能已被发送或状态已变化。");
inline const QString kResentQueuedMessagesPattern = QStringLiteral("已重发 %1 条排队消息");
inline const QString kPeerReadUpdated = QStringLiteral("对方已读更新");
inline const QString kOnlineSummaryPattern = QStringLiteral(" | 在线 %1/%2");
inline const QString kDetailRecallWindowLimited = QStringLiteral("仅支持撤回 2 分钟内发送的本人文本消息。");
inline const QString kDetailTargetConversationMissing = QStringLiteral("目标会话未在本地列表中");
inline const QString kDetailInvalidLink = QStringLiteral("链接无效");
inline const QString kDetailMissingAuthSession = QStringLiteral("缺少鉴权会话");
inline const QString kDetailDownloadFailedPattern = QStringLiteral("下载失败（HTTP %1）");
inline const QString kDetailSaveTempFileFailed = QStringLiteral("保存临时文件失败");
inline const QString kDetailOpenDownloadedFileFailed = QStringLiteral("系统无法打开下载文件");
inline const QString kDetailRecalledBySelf = QStringLiteral("你撤回了一条消息");
inline const QString kDetailRecalledByPeer = QStringLiteral("对方撤回了一条消息");
inline const QString kRecalledBySelfMessage = QStringLiteral("你撤回了一条消息");
inline const QString kRecalledByUnknownMessage = QStringLiteral("对方撤回了一条消息");
inline const QString kRecalledByUserPattern = QStringLiteral("%1 撤回了一条消息");
inline const QString kBackendRecalledSystemText = QStringLiteral("一条消息已撤回");
inline const QString kSystemMessagePattern = QStringLiteral("[系统] %1");

// Image preview dialog
inline const QString kImagePreviewTitle = QStringLiteral("图片预览");
inline const QString kOpenWithSystem = QStringLiteral("用系统打开");
inline const QString kClose = QStringLiteral("关闭");

// Profile dialog
inline const QString kProfileHint = QStringLiteral("点击更换头像，保存后其他客户端也会同步。");
inline const QString kChooseAvatarImage = QStringLiteral("选择头像图片");
inline const QString kImageFileFilter = QStringLiteral("图片文件 (*.png *.jpg *.jpeg *.webp *.gif *.bmp);;所有文件 (*.*)");

// Friends dialog
inline const QString kNoFriends = QStringLiteral("暂无好友。");
inline const QString kFriendsList = QStringLiteral("好友列表");
inline const QString kAddFriend = QStringLiteral("添加好友");
inline const QString kEnterPeerEmail = QStringLiteral("请输入对方邮箱：");

// Friend requests dialog
inline const QString kFriendRequests = QStringLiteral("好友申请");
inline const QString kNoPendingRequests = QStringLiteral("当前没有待处理申请。");
inline const QString kIncoming = QStringLiteral("收到");
inline const QString kOutgoing = QStringLiteral("发出");
inline const QString kAccept = QStringLiteral("同意");
inline const QString kReject = QStringLiteral("拒绝");
inline const QString kHandleFriendRequest = QStringLiteral("处理好友申请");
inline const QString kEnterRequestId = QStringLiteral("请输入要处理的申请 ID：");

// Blacklist dialog
inline const QString kBlacklist = QStringLiteral("黑名单");
inline const QString kBlacklistEmpty = QStringLiteral("黑名单为空。");
inline const QString kBlockUser = QStringLiteral("拉黑用户");
inline const QString kUnblockUser = QStringLiteral("解除拉黑");
inline const QString kEnterBlockEmail = QStringLiteral("请输入要拉黑的邮箱：");
inline const QString kNoUsersToUnblock = QStringLiteral("当前没有可解除的用户。");
inline const QString kUnblockDialogTitle = QStringLiteral("解除拉黑");
inline const QString kEnterUnblockUserId = QStringLiteral("请输入要解除拉黑的用户 ID：");

// Members dialog
inline const QString kOnline = QStringLiteral("在线");
inline const QString kOfflinePattern = QStringLiteral("离线（最后活跃 %1）");

// Logout
inline const QString kLogoutTitle = QStringLiteral("退出登录");
inline const QString kLogoutConfirm = QStringLiteral("确定要退出登录吗？");

// Delete conversation
inline const QString kDeleteConversationTitle = QStringLiteral("删除会话");
inline const QString kDeleteConversationConfirm = QStringLiteral("\u786e\u5b9a\u8981\u5220\u9664\u4f1a\u8bdd\u201c%1\u201d\u5417\uff1f");

// Edit message
inline const QString kEditMessageTitle = QStringLiteral("编辑消息");
inline const QString kEditMessagePrompt = QStringLiteral("请输入新的消息内容：");

// Search
inline const QString kGlobalSearchTitle = QStringLiteral("全局搜索");
inline const QString kGlobalSearchPrompt = QStringLiteral("请输入关键词");
inline const QString kGlobalSearchNotFound = QStringLiteral("\u672a\u627e\u5230\u5305\u542b\u201c%1\u201d\u7684\u6d88\u606f\u3002");
inline const QString kGlobalSearchResultTitle = QStringLiteral("\u5168\u5c40\u641c\u7d22\u7ed3\u679c");
inline const QString kGlobalSearchHitPattern = QStringLiteral("\u5173\u952e\u8bcd\u201c%1\u201d\u547d\u4e2d %2 \u6761");
inline const QString kLocateMessage = QStringLiteral("定位消息");

// Favorites
inline const QString kFavorites = QStringLiteral("收藏夹");
inline const QString kNoFavorites = QStringLiteral("当前没有收藏消息。");
inline const QString kFavoritesCountPattern = QStringLiteral("已收藏 %1 条消息");
inline const QString kRemoveFavorite = QStringLiteral("移除收藏");

// Message detail
inline const QString kMessageIdPattern = QStringLiteral("消息 ID: %1");

// Send file
inline const QString kSelectFileTitle = QStringLiteral("选择要发送的文件");
inline const QString kAllFilesFilter = QStringLiteral("所有文件 (*.*)");

// Queued messages dialog
inline const QString kResendAll = QStringLiteral("重发全部");
inline const QString kDeleteSelected = QStringLiteral("删除选中");

// Context menu
inline const QString kOpenFile = QStringLiteral("打开文件");
inline const QString kEditMessage = QStringLiteral("编辑消息");
inline const QString kRecallMessage = QStringLiteral("撤回消息");
inline const QString kUnfavoriteMessage = QStringLiteral("取消收藏");
inline const QString kFavoriteMessage = QStringLiteral("收藏消息");
inline const QString kCopyMessage = QStringLiteral("复制消息");
inline const QString kDeleteConversation = QStringLiteral("删除会话");
inline const QString kUnpinConversation = QStringLiteral("取消置顶");
inline const QString kPinConversation = QStringLiteral("置顶会话");
inline const QString kUnmuteConversation = QStringLiteral("取消免打扰");
inline const QString kMuteConversation = QStringLiteral("免打扰");

// Sidebar buttons
inline const QString kLogoutButton = QStringLiteral("退出登录");
inline const QString kFriendsButton = QStringLiteral("好友");
inline const QString kRequestsButton = QStringLiteral("申请");
inline const QString kBlacklistButton = QStringLiteral("黑名单");

// Search placeholders
inline const QString kSearchCurrentMessages = QStringLiteral("搜索当前会话消息");

// Search buttons
inline const QString kPrevButton = QStringLiteral("上一条");
inline const QString kNextButton = QStringLiteral("下一条");
inline const QString kGlobalSearchButton = QStringLiteral("全局搜索");
inline const QString kFavoritesButton = QStringLiteral("收藏夹");

// Send file button
inline const QString kSendFileButton = QStringLiteral("发送文件");

// Status messages
inline const QString kStatusImageOpened = QStringLiteral("状态：图片已打开");
inline const QString kStatusImageOpenFailed = QStringLiteral("状态：打开文件失败");

// Reply
inline const QString kReplyAction = QStringLiteral("回复");
inline const QString kReplyContentPattern = QStringLiteral("\u21a9 \u56de\u590d %1: %2\n%3");
inline const QString kReplyPreviewPattern = QStringLiteral("\u56de\u590d %1: %2");

// Create group dialog
inline const QString kCreateGroupTitle = QStringLiteral("\u521b\u5efa\u7fa4\u804a");
inline const QString kGroupNameLabel = QStringLiteral("\u7fa4\u540d\u79f0");
inline const QString kGroupNamePlaceholder = QStringLiteral("\u8f93\u5165\u7fa4\u804a\u540d\u79f0");
inline const QString kInviteMembersLabel = QStringLiteral("\u9080\u8bf7\u6210\u5458");
inline const QString kAddMemberEmailPlaceholder = QStringLiteral("\u8f93\u5165\u90ae\u7bb1\uff0c\u56de\u8f66\u6dfb\u52a0");
inline const QString kAddButton = QStringLiteral("\u6dfb\u52a0");

// Recall message confirm
inline const QString kRecallConfirmTitle = QStringLiteral("\u64a4\u56de\u6d88\u606f");
inline const QString kRecallConfirmPattern = QStringLiteral("\u786e\u5b9a\u8981\u64a4\u56de\u8fd9\u6761\u6d88\u606f\u5417\uff1f\n\n\"%1\"");

// Typing status
inline const QString kTypingSinglePattern = QStringLiteral("%1 \u6b63\u5728\u8f93\u5165...");
inline const QString kTypingDualPattern = QStringLiteral("%1\u3001%2 \u6b63\u5728\u8f93\u5165...");
inline const QString kTypingMultiplePattern = QStringLiteral("%1 \u7b49 %2 \u4eba\u6b63\u5728\u8f93\u5165...");

// Profile dialog extras
inline const QString kNicknamePlaceholder = QStringLiteral("\u8f93\u5165\u65b0\u6635\u79f0");
inline const QString kNicknameLabel = QStringLiteral("\u6635\u79f0");
inline const QString kEmailLabelShort = QStringLiteral("\u90ae\u7bb1");

// Friends dialog extras
inline const QString kAddFriendPlaceholder = QStringLiteral("\u8f93\u5165\u5bf9\u65b9\u90ae\u7bb1\u6dfb\u52a0\u597d\u53cb");
inline const QString kNoFriendRequests = QStringLiteral("\u6682\u65e0\u597d\u53cb\u8bf7\u6c42");

// Blacklist dialog extras
inline const QString kBlockEmailPlaceholder = QStringLiteral("\u8f93\u5165\u90ae\u7bb1\u62c9\u9ed1\u7528\u6237");
inline const QString kBlockButton = QStringLiteral("\u62c9\u9ed1");
inline const QString kBlacklistEmptyItem = QStringLiteral("\u9ed1\u540d\u5355\u4e3a\u7a7a");

// Group member operations
inline const QString kNotGroupChatStatus = QStringLiteral("\u5f53\u524d\u4e0d\u662f\u7fa4\u804a");
inline const QString kInviteEmailPrompt = QStringLiteral("\u8f93\u5165\u8981\u9080\u8bf7\u7684\u90ae\u7bb1\uff08\u591a\u4e2a\u7528\u9017\u53f7\u5206\u9694\uff09");
inline const QString kNoMemberInfo = QStringLiteral("\u6682\u65e0\u6210\u5458\u4fe1\u606f");
inline const QString kNoRemovableMembers = QStringLiteral("\u6ca1\u6709\u53ef\u79fb\u9664\u7684\u6210\u5458");
inline const QString kSelectMemberToRemove = QStringLiteral("\u9009\u62e9\u8981\u79fb\u9664\u7684\u6210\u5458");
inline const QString kConfirmRemoveTitle = QStringLiteral("\u786e\u8ba4\u79fb\u9664");
inline const QString kConfirmRemoveMemberPattern = QStringLiteral("\u786e\u5b9a\u8981\u5c06 %1 \u79fb\u51fa\u7fa4\u804a\u5417\uff1f");
inline const QString kGroupMembersTitlePattern = QStringLiteral("\u7fa4\u6210\u5458 - %1");
inline const QString kSelfTagShort = QStringLiteral(" (\u6211)");
inline const QString kOnlineSuffix = QStringLiteral(" \u5728\u7ebf");
inline const QString kOfflineSuffix = QStringLiteral(" \u79bb\u7ebf");

// Leave group confirm
inline const QString kLeaveGroupConfirmPattern = QStringLiteral("\u786e\u5b9a\u8981\u9000\u51fa\u300c%1\u300d\u5417\uff1f");

// Delete conversation confirm pattern (with name)
inline const QString kDeleteConversationConfirmPattern = QStringLiteral("\u786e\u5b9a\u8981\u5220\u9664\u300c%1\u300d\u5417\uff1f");

// Conversation meta
inline const QString kGroupChatType = QStringLiteral("\u7fa4\u804a");
inline const QString kPrivateChatType = QStringLiteral("\u79c1\u804a");
inline const QString kMemberCountMetaPattern = QStringLiteral(" \u00b7 %1 \u4eba");
inline const QString kOnlineCountMetaPattern = QStringLiteral(" \u00b7 %1 \u5728\u7ebf");
}

namespace MessageHandler
{
inline const QString kSendFileDialogTitle = QStringLiteral("发送文件");
inline const QString kCannotOpenFile = QStringLiteral("无法打开文件。");
inline const QString kFileEmpty = QStringLiteral("文件为空，无法发送。");
inline const QString kFileTooLarge = QStringLiteral("文件过大，当前仅支持不超过 8MB 的文件。");
inline const QString kEditNotSupported = QStringLiteral("消息编辑功能需要后端支持");
}

namespace ConversationManager
{
inline const QString kYouPrefix = QStringLiteral("你: %1");
inline const QString kConversationDeleted = QStringLiteral("已删除会话");
inline const QString kUnpinned = QStringLiteral("已取消置顶");
inline const QString kPinned = QStringLiteral("已置顶");
inline const QString kUnmuted = QStringLiteral("已取消免打扰");
inline const QString kMuted = QStringLiteral("已设置免打扰");
}

namespace NetworkService
{
inline const QString kCannotOpenAvatar = QStringLiteral("无法打开头像图片。");
inline const QString kAvatarEmpty = QStringLiteral("头像图片为空。");
inline const QString kAvatarTooLarge = QStringLiteral("头像图片过大，当前仅支持不超过 8MB 的图片。");
inline const QString kSelectImageFile = QStringLiteral("请选择图片文件作为头像。");
inline const QString kUploadingAvatar = QStringLiteral("状态：正在上传头像");
}

namespace MediaUtils
{
inline const QString kImagePreviewPrefix = QStringLiteral("[\u56fe\u7247]");
inline const QString kFilePreviewPrefix = QStringLiteral("[\u6587\u4ef6]");
inline const QString kSelfPrefix = QStringLiteral("\u4f60: %1");
inline const QString kSystemMessagePreviewPattern = QStringLiteral("\u7cfb\u7edf\u6d88\u606f: %1");
}

namespace ProfileManager
{
inline const QString kTypingSinglePattern = QStringLiteral("%1 \u6b63\u5728\u8f93\u5165...");
inline const QString kTypingMultiplePattern = QStringLiteral("%1 \u7b49\u4eba\u6b63\u5728\u8f93\u5165...");
}
}



