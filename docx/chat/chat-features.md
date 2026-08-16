# JettyCat 聊天功能汇总

> 本文档汇总当前 JettyCat 聊天后端的功能现状，用于对照普通 IM 产品做差距分析。
> 数据来源：`code/chat/`（REST Controller）、`code/service/`（业务服务）、`code/dao/`（数据访问）、`code/chat/connection.cc`（WebSocket）。

## 一、现有 Service 功能汇总

### 1. `FriendService`（好友关系）

| 方法 | REST 接口 | 功能 | 说明 |
|------|-----------|------|------|
| `addFriend` | `POST /chat/add_friend` | 添加好友 | 直接添加，无对方确认；校验目标存在 + 防自加；`user_friend` 表 CHECK 约束 `user_id < friend_id` |
| `removeFriend` | `POST /chat/remove_friend` | 删除好友 | 标记删除（`status=0`），非物理删除；无对方通知 |
| `getFriendList` | `GET /chat/friend_list` | 获取好友列表 | 双向 union 查询（因 CHECK 约束），返回 friend_id/username/nickname/avatar_url |

### 2. `GroupService`（群组）

| 方法 | REST 接口 | 功能 | 说明 |
|------|-----------|------|------|
| `addGroup` | `POST /chat/add_group` | 加入群聊 | 校验群存在；`identity` 固定为 `member`；无群主/管理员概念 |
| `removeGroup` | `POST /chat/remove_group` | 退出群聊 | 物理删除（`user_group` 表无 status 字段） |
| `getGroupList` | `GET /chat/group_list` | 获取群聊列表 | 返回 group_id/group_name/identity/join_time |

### 3. `MessageService`（消息查询，REST 历史拉取）

| 方法 | REST 接口 | 功能 | 说明 |
|------|-----------|------|------|
| `fetchUserMessage` | `GET /chat/fetch_user_message` | 拉取私聊历史 | 分页（每页 10 条）；receiverId 越权保护 |
| `fetchGroupMessage` | `GET /chat/fetch_group_message` | 拉取群聊历史 | 分页（每页 10 条） |

### 4. WebSocket 消息处理（`connection.cc`，非 Service 但属聊天功能）

| 功能 | 实现 | 说明 |
|------|------|------|
| 连接认证 | `co_onOpen` | JWT 校验 → 查 user_id → Redis 会话注册 → 欢迎消息 |
| 私聊实时转发 | `handlePrivateMessage` | 校验发送者 → DB 持久化 → Redis 查目标 session → 逐个转发 |
| 群聊实时广播 | `handleGroupMessage` | 校验发送者 → 校验群成员 → DB 归档 → 广播给所有在线群成员（跳过发送者） |
| 消息路由分发 | `WsMessageRouter::dispatch` | 按 `type` 分发：`private_message` / `group_message` |
| 心跳保活 | `co_onPong` | 每 1000 次 Pong 刷新一次 Redis TTL |
| 会话清理 | `co_onClose` / `co_onBadClose` | SREM 删除 Redis 会话映射 |
| 群成员查询 | `GroupDao::listGroupMembers` | 查 `user_group` 表返回群成员 user_id 列表（供广播） |

---

## 二、与普通 IM 产品相比缺少的功能

> 「缺失」指后端尚未实现；「部分」指数据结构预留了字段但未走通完整链路。

| # | 缺失功能 | 现状 | 影响/说明 |
|---|----------|------|-----------|
| 1 | 群聊实时消息推送 | ✅ 已实现 | `handleGroupMessage` + `group_message` 路由，群成员实时广播 + DB 归档 |
| 2 | 好友申请/同意机制 | 缺失 | `addFriend` 直接建立关系，无申请/同意流程与通知 |
| 3 | 删除/退出的对方通知 | 缺失 | 删好友、退群时对方无感知 |
| 4 | 消息已读/未读回执 | 缺失 | 前端 unread 纯本地维护，后端无已读状态 |
| 5 | 群聊管理 | 缺失 | 无创建群、群主/管理员、踢人、禁言、群公告 |
| 6 | 群成员列表查询 | 部分 | `GroupDao::listGroupMembers` 已实现（供广播），但无对外 REST 接口 |
| 7 | 消息撤回/编辑/删除 | 缺失 | 消息发出后无法撤回删除 |
| 8 | 富媒体真实收发 | 部分 | `Message.Type` 有 IMAGE/VOICE/VIDEO/FILE，但仅字符串透传，无文件上传/OSS 等 |
| 9 | 在线状态/上线通知 | 部分 | Redis 存了在线 session，但不对外暴露，无上线/下线推送 |
| 10 | 正在输入/已送达 | 缺失 | 无 typing 指示、送达回执 |
| 11 | 消息搜索 | 缺失 | 无关键词搜索 |
| 12 | 多端同步/记录漫游 | 部分 | 消息入库可漫游，但无多端已读同步/增量拉取接口 |
| 13 | 会话列表管理 | 缺失 | 无置顶、免打扰、会话归档 |
| 14 | 离线消息主动推送 | 部分 | 靠上线后拉取，无主动 Push |
| 15 | 黑名单/拉黑 | 部分 | `removeFriend` 的 `status=0` 语义混用删除与拉黑，无独立黑名单 |
| 16 | 敏感词/内容审核 | 缺失 | 无内容过滤 |
| 17 | 分页方向 | 部分 | 仅支持向前翻旧，不支持加载最新/跳页 |

### 缺失功能优先级建议（按开发量从小到大）

| 优先级 | 功能 | 理由 |
|--------|------|------|
| ⭐⭐⭐ | 好友申请/同意 + 通知 | 补齐社交闭环 |
| ⭐⭐⭐ | 在线状态 + 上线/下线推送 | Redis 已有数据，加查询/广播即可 |
| ⭐⭐ | 消息已读回执 | 单聊体验刚需 |
| ⭐ | 群管理、搜索、富媒体、撤回 | 工作量大，视产品定位再定 |

---

## 三、附：现有接口统一响应格式

所有 REST 接口统一返回 `{code, status, msg, data}`（详见 `rest-api.md`）：

- `code`：HTTP 状态码（200/400/403/404/500）
- `status`：由 code 推导（success / failed / error）
- `msg`：人类可读描述
- `data`：业务数据（无数据时为 null）

---

## 四、待办列表

| # | 待办项 | 说明 |
|---|--------|------|
| 1 | 加群/退群实时同步群集合 | 用户在线期间通过 `addGroup`/`removeGroup` 加/退群后，Redis 群聊在线集合 `{instanceId}_group_{groupId}` 未同步，需重连才生效。应在 `GroupService::addGroup/removeGroup` 中同步 SADD/SREM 该集合 |

