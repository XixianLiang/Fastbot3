#!/usr/bin/env bash
#
# 简单测试脚本：验证设备上 ADBKeyBoard (com.android.adbkeyboard/.AdbIME) 是否能正常接收并输入文本。
#
# 重要：浏览器地址栏/搜索框（如 Chrome「Search or type URL」）多为自定义或 WebView，
# 往往不会把输入交给系统 IME，因此 ADBKeyBoard 的 commitText 无法写入。
# 请先用「系统备忘录/便签」或「设置 → 搜索」等标准输入框测试。
#

set -e

IME_ID="com.android.adbkeyboard/.AdbIME"
PKG="com.android.adbkeyboard"

echo "=== ADBKeyBoard 测试脚本 ==="
echo ""
echo "请务必使用【系统备忘录/便签】或【设置里的搜索框】测试，不要用浏览器地址栏。"
echo "原因：浏览器输入框多不接系统 IME，ADBKeyBoard 无法把文字写进去。"
echo ""

# 1. 检查设备
if ! adb shell true 2>/dev/null; then
    echo "[FAIL] 未检测到设备或 adb 不可用，请先执行: adb devices"
    exit 1
fi
echo "[OK] 设备已连接"
echo ""

# 2. 启用并切换为 ADBKeyBoard
echo "--- 启用并设置 IME 为 ADBKeyBoard ---"
adb shell ime enable "$IME_ID" || true
adb shell ime set "$IME_ID"
echo "[OK] 当前 IME 已设为: $IME_ID"
echo ""

echo ">>> 请现在在设备上：打开【备忘录/便签】或【设置→搜索】，点进输入框获得焦点 <<<"
echo ">>> 准备好后按回车继续... <<<"
read -r

# 3. 测试 ASCII 文本 (ADB_INPUT_TEXT)
echo ""
echo "--- 测试 1: 发送 ASCII 文本 (ADB_INPUT_TEXT) ---"
adb shell am broadcast -a ADB_INPUT_TEXT --es msg "hello_from_shell" -p "$PKG"
echo "[OK] 已发送 ASCII: hello_from_shell"
echo "请在设备输入框中查看是否出现: hello_from_shell"
echo ""

# 4. 测试中文 (ADB_INPUT_B64，与 Fastbot 一致)
echo "--- 测试 2: 发送中文 (ADB_INPUT_B64，与 Fastbot 一致) ---"
CHINESE="通勤必听"
# 使用 Python 保证 UTF-8 + Base64 与 Java Base64.NO_WRAP 一致
B64=$(python3 -c "import base64; print(base64.b64encode('$CHINESE'.encode('utf-8')).decode())")
adb shell am broadcast -a ADB_INPUT_B64 --es msg "$B64" -p "$PKG"
echo "[OK] 已发送 B64 中文: $CHINESE (base64=$B64)"
echo "请在设备输入框中查看是否出现: $CHINESE"
echo ""

echo "=== 测试完成 ==="
echo "若在【备忘录/设置搜索】里能看到 hello_from_shell 和 通勤必听，说明 adbkeyboard 工作正常。"
echo "若在浏览器里不显示是正常的（浏览器通常不支持）；若在备忘录里也不显示，请检查："
echo "  1. 已安装 ADBKeyBoard（如运行过 activate_fastbot.sh）"
echo "  2. 设置 IME 后是否【先点了一下输入框】再按回车"
echo "  3. 部分机型需在 设置→语言与输入法 里手动启用 ADBKeyBoard"
