AI Agent 正在从云端走向端侧。但大部分端智能体在应用时，集中在PC端，并没有把人们解放出来。。

# ai_agent_assistant 是运行在 openvela 嵌入式系统上的 AI Agent 助手，专为ai_agent设计。它的核心差异化在于：

主动任务机制：Agent 不等用户开口，根据时间和条件主动推送
Router 意图路由：区分「记一下」vs「提醒我」vs「总结一下」，智能分流
Tool/Shell 调用：读传感器、写文件、发通知、执行命令——Agent 能动手
自然语言→结构化输出：语音输入自动解析为待办/日程/备忘
多渠道 + 多设备协作：飞书、微信、语音、MQTT、Node 协议

# ai_agent_assistant

openvela/NuttX application for a 6-inch parallel e-paper panel. The main controller only sends framed commands over SPI; the e-paper driver board owns waveform generation, framebuffer conversion, and panel timing.

Run `epaper_todo mcp` to expose the Xiaozhi MCP JSON-RPC bridge on stdin/stdout. The bridge implements `todo_add`, `todo_update`, `todo_delete`, and `todo_refresh`. A host can therefore map voice intents such as “add buy milk” or “mark item 3 done” to MCP tool calls.
