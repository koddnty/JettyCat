# chatter 接口定义

## websocket 通信格式定义

``` json 
{
    "code" : 200,
    "from" : "username(system)",
    "to" : "username(system)",
    "message" : "hello world",
    "reason" : "ok"
}
```

## HTTP REST 接口

### 用户历史消息拉取

**接口**: `GET /chat/fetch_user_message`

**认证**: Cookie 中携带 `jwttoken` (HttpOnly, SameSite=Strict)

**说明**: 拉取"**某位发送者发给我的**"私聊消息,即 `sender_id` 由前端传入,`receiver_id` 由 JWT 中的 `userid` 决定,不接受前端传入接收者 id。

**请求参数** (URL Query Params):

```
GET /chat/fetch_user_message?senderId=17&offset=0
```

| 参数名 | 类型 | 必填 | 含义 |
|--------|------|------|------|
| `senderId` | int | 是 | 消息发送者 id(聊天对方) |
| `offset` | int | 否 | 分页偏移量, 默认 0。每页返回条数由配置文件 `chatter.single_fetch_count` 控制 |

**响应体** (JSON):

成功 (200):
``` json
{
    "status": "success",
    "messages": [
        {
            "date": 1234567890,
            "from": 17,
            "type": "TEXT",
            "content": "hello"
        }
    ],
    "count": 1
}
```

| 字段名 | 类型 | 含义 |
|--------|------|------|
| `status` | string | "success" / "error" / "failed" |
| `messages` | array | 消息列表, 按 `send_time` 降序排列 |
| `messages[].date` | int | 发送时间的 UNIX 时间戳 (秒) |
| `messages[].from` | int | 发送者用户 id (即 `senderId`, 真正发消息的人) |
| `messages[].type` | string | 消息类型: `TEXT`, `IMAGE`, `VOICE`, `VIDEO`, `FILE` |
| `messages[].content` | string | 消息内容 |
| `count` | int | 本页返回的消息条数 |

失败 (403 — JWT 缺失或无效):
``` json
{
    "status": "failed",
    "error": "FORBIDDEN: Invalid or missing JWT token"
}
```

失败 (400 — 参数错误):
``` json
{
    "status": "failed",
    "error": "BAD_REQUEST: Missing or invalid 'senderId'"
}
```

失败 (500 — 数据库错误):
``` json
{
    "status": "error",
    "error": "Database query timeout"
}
```

---

### 群组历史消息拉取

**接口**: `GET /chat/fetch_group_message`

**认证**: Cookie 中携带 `jwttoken` (HttpOnly, SameSite=Strict)

**请求参数** (URL Query Params):

```
GET /chat/fetch_group_message?groupId=20&offset=0
```

| 参数名 | 类型 | 必填 | 含义 |
|--------|------|------|------|
| `groupId` | int | 是 | 目标群组 id |
| `offset` | int | 否 | 分页偏移量, 默认 0。每页返回条数由配置文件 `chatter.single_fetch_count` 控制 |

**响应体** (JSON):

成功 (200):
``` json
{
    "status": "success",
    "messages": [
        {
            "date": 1234567890,
            "from": 17,
            "type": "TEXT",
            "content": "hello group"
        }
    ],
    "count": 1
}
```

| 字段名 | 类型 | 含义 |
|--------|------|------|
| `status` | string | "success" / "error" / "failed" |
| `messages` | array | 消息列表, 按 `send_time` 降序排列 |
| `messages[].date` | int | 发送时间的 UNIX 时间戳 (秒) |
| `messages[].from` | int | 发送者用户 id |
| `messages[].type` | string | 消息类型: `TEXT`, `IMAGE`, `VOICE`, `VIDEO`, `FILE` |
| `messages[].content` | string | 消息内容 |
| `count` | int | 本页返回的消息条数 |

失败 (403 — JWT 缺失或无效):
``` json
{
    "status": "failed",
    "error": "FORBIDDEN: Invalid or missing JWT token"
}
```

失败 (400 — 参数错误):
``` json
{
    "status": "failed",
    "error": "BAD_REQUEST: Missing or invalid 'groupId'"
}
```

失败 (500 — 数据库错误):
``` json
{
    "status": "error",
    "error": "Database query failed"
}
```
