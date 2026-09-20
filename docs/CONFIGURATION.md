# Configuration / 配置

从掌机设置菜单调整语言、输入方案、音量、亮度、休眠和 UI。确认保存后由配置服务验证并持久化；取消不保存草稿。配置键及合法范围以 `system/config-core/schema/gdkmini.schema` 为准。

`input_style` 支持 `raw`、`xbox`、`ps`。原始透传保留模拟器输入行为；其他方案按路由配置提供原生菜单操作。路由默认文件为 `/etc/gkd-mini/input-routing.conf`，受信任的用户覆盖为 `/media/data/local/etc/gkd-mini/input-routing.conf`。具体按键由应用路由决定，不能假定所有游戏都用 START 开始。

截图组合键为 MENU＋L1，可配置为 NONE 禁用。截图写入游戏卡 `screenshots`。普通操作结果共用 SUCCESS/FAILED；截图保留 SHOT SAVED/SHOT FAILED。

系统菜单等待明确操作，无菜单倒计时。`auto_suspend_timeout_seconds=0` 禁用自动休眠。临界低电量保护与普通自动休眠是不同机制，接电/读数/事务状态等条件参与保护判断。5% 开始显示 POWER LOW；开机保留一次电量显示。

文本信息区域只显示英文可打印内容，过滤有效非 ASCII 文本；菜单仍可用支持的中/英文固定字形。文本自动换行并在超出可见区域时显示滚动条。更新确认信息来自校验后的更新包元数据，不是固定示例。

游戏卡缺失时显示系统全屏提示，普通确认/取消/MENU 不能关闭；电源菜单仍可使用。USB STORAGE 导出会由系统协调卸载/占用，不能把正常导出状态当作物理拔卡。

卡健康检测已经取消，没有对应的健康评分或开关。
