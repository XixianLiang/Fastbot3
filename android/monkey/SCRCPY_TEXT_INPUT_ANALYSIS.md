# scrcpy 文本输入（Input Text）实现分析

本文档基于对 [scrcpy](https://github.com/Genymobile/scrcpy) 源码的阅读，说明 scrcpy 如何实现输入文本，包括英文、中文、表情符号等的支持方式与限制。

---

## 一、总体架构：三种键盘模式

scrcpy 提供三种键盘输入模式，对应不同的输入路径和字符支持能力：

| 模式 | 参数 | 实现层级 | 中文/表情/IME |
|------|------|----------|----------------|
| **SDK** | `--keyboard=sdk`（默认） | Android API 注入 KeyEvent/文本 | **受限**（见下） |
| **UHID** | `--keyboard=uhid` / `-K` | 设备端模拟物理 HID 键盘 | **支持**（含 IME） |
| **AOA** | `--keyboard=aoa` | USB AOAv2 HID，不经过 server | 仅 USB，需配置布局 |

文档明确说明：**SDK 模式** “limited to ASCII and some other characters”；**UHID 模式** “it works for all characters and IME (contrary to --keyboard=sdk)”（见 `doc/keyboard.md`）。  
下面重点分析 **SDK 模式** 下“输入文本”的完整链路（这也是直接“打字符”到设备的主要路径），并说明对中文、英文、表情符号的实际行为。

---

## 二、SDK 模式下的文本输入链路

### 2.1 客户端（PC）：从 SDL 到控制消息

1. **SDL 文本事件**  
   - 当用户在有焦点的窗口中输入时，SDL 会产生 `SDL_TEXTINPUT`。  
   - `SDL_TextInputEvent` 中的 `text` 是 **UTF-8 字符串**（可包含多字节字符，如中文、emoji）。

2. **输入分发**（`app/src/input_manager.c`）  
   - `sc_input_manager_handle_event()` 收到 `SDL_TEXTINPUT` 后调用 `sc_input_manager_process_text_input()`。  
   - 若当前是 SDK 键盘且支持 `process_text`，则把 `event->text`（UTF-8）封装成 `sc_text_event`，交给 key processor。

3. **SDK 键盘处理**（`app/src/keyboard_sdk.c`）  
   - `sc_key_processor_process_text()`：  
     - 若为 `SC_KEY_INJECT_MODE_RAW`：**不**发文本，直接 return。  
     - 若为 `SC_KEY_INJECT_MODE_MIXED`：对**字母和空格**不发文本（交给 key 事件路径），其余字符发文本。  
     - 否则（如 `SC_KEY_INJECT_MODE_TEXT`）：所有可打印字符都走文本注入。  
   - 构造 `SC_CONTROL_MSG_TYPE_INJECT_TEXT`，`msg.inject_text.text = strdup(event->text)`，即**整段 UTF-8 字符串**送入控制器。

4. **控制消息序列化**（`app/src/control_msg.c`）  
   - `sc_control_msg_serialize()` 对 `INJECT_TEXT`：  
     - 使用 `write_string()`：先 4 字节大端长度，再写入 UTF-8 字节。  
     - 长度由 `write_string_payload()` 决定，内部用 `sc_str_utf8_truncation_index(utf8, max_len)` 在 **不截断多字节 UTF-8 字符** 的前提下截断到最多 `SC_CONTROL_MSG_INJECT_TEXT_MAX_LENGTH`（300）字节。  
   - 因此：**整条链路上文本始终是 UTF-8**，支持任意 Unicode 码点（包括中文、emoji），仅在长度上限制为 300 字节。

小结（客户端）：  
- 输入来源：SDL `SDL_TEXTINPUT`，UTF-8。  
- 行为：根据 `key_inject_mode` 决定哪些键走“文本”路径；走文本路径时，整串 UTF-8 作为 `INJECT_TEXT` 发往 server。  
- 传输：UTF-8，按码点安全截断，最大 300 字节。

---

### 2.2 服务端（Android）：从控制消息到系统注入

1. **反序列化**（`server/.../ControlMessageReader.java`）  
   - `parseInjectText()` → `parseString()`：先读 4 字节长度，再读对应字节，`new String(data, StandardCharsets.UTF_8)`。  
   - 得到的是**完整 Unicode 字符串**（可含 BMP 与增补平面，如 emoji）。

2. **Controller 处理**（`server/.../Controller.java`）  
   - `handleEvent()` 收到 `TYPE_INJECT_TEXT` 后调用 `injectText(msg.getText())`。

3. **injectText 的实现**（关键逻辑）  
   ```java
   private int injectText(String text) {
       int successCount = 0;
       for (char c : text.toCharArray()) {
           if (!injectChar(c)) {
               Ln.w("Could not inject char u+" + String.format("%04x", (int) c));
               continue;
           }
           successCount++;
       }
       return successCount;
   }
   ```  
   - 使用 `text.toCharArray()` 遍历的是 **UTF-16 码元（char）**，不是 Unicode 码点（code point）。  
   - 对 emoji 等增补平面字符（如 U+1F600），在 Java 中会变成两个 char（高、低代理），**分别**调用 `injectChar()`，导致：  
     - 单次传入的是半个代理对，`KeyCharacterMap.getEvents(char[])` 无法为单独代理返回合法按键序列；  
     - 通常返回 null，`injectChar` 失败，该“字符”被跳过并打日志。

4. **injectChar 的实现**  
   ```java
   private boolean injectChar(char c) {
       String decomposed = KeyComposition.decompose(c);
       char[] chars = decomposed != null ? decomposed.toCharArray() : new char[]{c};
       KeyEvent[] events = charMap.getEvents(chars);
       if (events == null) {
           return false;
       }
       // ... 对每个 KeyEvent 调用 Device.injectEvent(event, ...)
   }
   ```  
   - `KeyCharacterMap charMap = KeyCharacterMap.load(KeyCharacterMap.VIRTUAL_KEYBOARD)`：虚拟键盘的键位-字符映射。  
   - `KeyComposition.decompose(c)`：对**西文带调字符**（如 é→ 组合符+基础字母）做 NFD 式分解，以便 `getEvents()` 能返回按键序列（见 `KeyComposition.java` 注释）。  
   - `charMap.getEvents(chars)`：  
     - 若该字符可由“虚拟键盘”的按键组合产生（如 ASCII、拉丁扩展），则返回对应 `KeyEvent[]`，再通过 `Device.injectEvent()` 注入。  
     - 若**不能**（如 CJK 汉字、emoji、单独代理），则返回 **null**，`injectChar` 返回 false，该字符被跳过。

5. **最终注入**（`Device.java` / `InputManager.java`）  
   - `Device.injectKeyEvent()` 构造 `KeyEvent`（含 `KeyCharacterMap.VIRTUAL_KEYBOARD`），再调用 `InputManager.injectInputEvent(event, mode)`。  
   - 底层是 `InputManager.injectInputEvent(InputEvent, int)`，通过反射调用 `android.hardware.input.InputManager.injectInputEvent()`，即标准 Android 输入注入 API。

小结（服务端）：  
- 协议与解码：**支持任意 UTF-8 文本**（含中文、emoji）。  
- 实际注入：**仅对“可由 KeyCharacterMap.getEvents() 产生按键序列”的字符有效**，即主要为 ASCII + 西文扩展（经 KeyComposition 分解）。  
- 中文、emoji、单独代理等：`getEvents()` 返回 null，**无法注入**，只会打 “Could not inject char u+xxxx” 并跳过。

---

## 三、英文、中文、表情符号的实际行为

### 3.1 英文（及西文扩展）

- **Key 事件路径**（默认 MIXED）：  
  - 字母、空格等先走 `SDL_KEYDOWN/KEYUP` → `keyboard_sdk` 的 `process_key` → 映射为 Android keycode → `INJECT_KEYCODE`。  
  - 数字、标点等可走 `SDL_TEXTINPUT` → `INJECT_TEXT`。  
- **Text 事件路径**（`INJECT_TEXT`）：  
  - 服务端用 `KeyCharacterMap.getEvents(chars)` + `KeyComposition.decompose()` 把字符转成 KeyEvent 序列再注入。  
- 带调字符（如 é, ñ）：通过 `KeyComposition` 分解后，通常能正确得到按键序列并注入。  
- **结论**：英文及常见西文扩展在 SDK 模式下可以正常输入。

### 3.2 中文（CJK）

- 汉字**无法**由“虚拟键盘”的单个按键组合表示，`KeyCharacterMap.getEvents(char[])` 对汉字会返回 **null**。  
- 因此即使用户在 PC 端用中文输入法输入了中文，并且这段 UTF-8 正确传到服务端，在 `injectText()` 里也会在 `injectChar(c)` 时失败，每个汉字被跳过并打警告。  
- 官方文档也明确：SDK 模式 “limited to ASCII and some other characters”。  
- **若要在设备上输入中文**：应使用 **UHID 模式**（`--keyboard=uhid`），让设备端把 scrcpy 当作物理键盘，由设备上的 IME 处理输入，从而支持中文等所有字符。

### 3.3 表情符号（Emoji，增补平面）

- 在 Java 中，emoji 等增补平面字符用 **两个 char（代理对）** 表示。  
- `injectText()` 使用 `for (char c : text.toCharArray())`，会**逐 char** 调用 `injectChar(c)`，即把**高、低代理分别**当作“一个字符”传入。  
- 对单独一个代理，`KeyCharacterMap.getEvents(chars)` 无法生成合法按键，返回 null，两个代理都会被跳过。  
- 因此：**SDK 模式下 emoji 无法通过 INJECT_TEXT 正确注入**；若要输入 emoji，同样需依赖 UHID + 设备 IME，或通过剪贴板粘贴等其它方式。

---

## 四、与“按键”路径的配合：prefer-text / raw-key-events

- **默认**：`key_inject_mode == SC_KEY_INJECT_MODE_MIXED`  
  - 字母、空格 → key 事件（便于游戏等场景的按键语义）；  
  - 数字、标点等 → text 事件。  
- **`--prefer-text`**：`SC_KEY_INJECT_MODE_TEXT`  
  - 字母、空格也走 text 事件；有利于某些应用正确显示字符，但游戏中的按键行为会变差。  
- **`--raw-key-events`**：`SC_KEY_INJECT_MODE_RAW`  
  - 全部走 key 事件，**不再**发送 `INJECT_TEXT`（`process_text` 直接 return）。  
- 以上只影响“何时发文本”；一旦发的是 `INJECT_TEXT`，服务端对中文/emoji 的限制不变（仍依赖 `getEvents()`，仅西文可注入）。

---

## 五、UHID 模式简要（支持中文与 IME）

- 客户端：键盘事件被转换成 **HID 报文**（如 `keyboard_uhid.c` 中的 `sc_hid_keyboard_generate_input_from_key`），通过 `SC_CONTROL_MSG_TYPE_UHID_INPUT` 发到 server。  
- 服务端：在设备上创建 UHID 键盘设备，把 HID 报文写入该设备；系统将其视为**物理键盘**。  
- 设备上焦点在输入框时，系统会使用 **IME**（输入法），因此可以输入中文、emoji 等；文档明确 “it works for all characters and IME”。  
- 使用 UHID 时需在设备上配置与 PC 一致的键盘布局（如 `doc/keyboard.md` 所述）。

---

## 六、UTF-8 与长度限制汇总

| 环节 | 编码 | 长度/截断 |
|------|------|-----------|
| SDL `SDL_TextInputEvent.text` | UTF-8 | 由 SDL/系统决定 |
| `INJECT_TEXT` 序列化（客户端） | UTF-8 | `sc_str_utf8_truncation_index()`，最大 300 字节，不截断多字节字符中间 |
| `ControlMessageReader.parseString()`（服务端） | UTF-8 → Java String | 与上面长度一致 |
| `injectText()` 遍历 | String → char[]（UTF-16 码元） | 按 char 逐个注入，emoji 被拆成两个无效 char |

---

## 七、结论表

| 内容类型 | SDK 模式（INJECT_TEXT） | UHID 模式 |
|----------|---------------------------|-----------|
| 英文 / 数字 / 标点 | ✅ 支持（key 或 text 路径） | ✅ 支持 |
| 西文带调（é, ñ 等） | ✅ 支持（KeyComposition + getEvents） | ✅ 支持 |
| 中文（CJK） | ❌ 不支持（getEvents 返回 null） | ✅ 支持（设备 IME） |
| 表情符号（emoji） | ❌ 不支持（toCharArray 拆成代理，getEvents 失败） | ✅ 支持（设备 IME） |

**实现要点总结：**

1. **客户端**：SDL 提供 UTF-8 文本 → 按 `key_inject_mode` 决定是否发 `INJECT_TEXT`；序列化时用 UTF-8 安全截断，最大 300 字节。  
2. **服务端**：将 UTF-8 解码为 String 后，用 `toCharArray()` 按 **char** 遍历，再用 `KeyCharacterMap.getEvents()` 转为 KeyEvent 注入；只有“虚拟键盘能拼出的”字符（主要是 ASCII + 西文扩展）能成功注入。  
3. **中文与 emoji**：在 SDK 模式下无法通过 `INJECT_TEXT` 正确输入；需使用 **UHID**（或 AOA）并依赖设备端 IME 才能实现完整多语言与表情输入。

---

## 八、Fastbot3 与 ADBKeyBoard：中文/表情输入方案

Fastbot3 需要支持输入中文等非 ASCII 文本（如自动化填表、搜索）。scrcpy 的 SDK 模式无法直接注入中文/emoji，而 UHID 需要用户配置物理键盘布局且主要面向“键位模拟”。Fastbot3 采用 **ADBKeyBoard**（[senzhk/ADBKeyBoard](https://github.com/senzhk/ADBKeyBoard)）作为 IME，通过 **广播 Intent** 把任意 Unicode 文本交给 ADBKeyBoard，由 ADBKeyBoard 通过 `InputConnection.commitText()` 写入当前焦点输入框，从而支持中文、表情符号等。

### 8.1 ADBKeyBoard 原理简述

- ADBKeyBoard 是一个 **Android 输入法**（IME），ID 为 `com.android.adbkeyboard/.AdbIME`。
- 安装并启用后，通过 **广播** 接收以下 Action（与 Fastbot3 中常量对应）：
  - **ADB_INPUT_TEXT**（`IME_MESSAGE`）：`Intent.getStringExtra("msg")` → `InputConnection.commitText(msg, 1)`。纯 UTF-8 字符串；经 **adb shell am broadcast --es msg '...'** 时，在 Oreo/P 上可能无法正确传递 UTF-8（README 注明 “This may not work on Oreo/P”）。
  - **ADB_INPUT_B64**（`IME_MESSAGE_B64`）：`Intent.getStringExtra("msg")` 为 Base64 字符串，解码为 UTF-8 后再 `commitText`。用于 **可靠传递中文、emoji**，避免 shell/Intent 对 UTF-8 的截断或乱码。
  - **ADB_INPUT_CHARS**（`IME_CHARS`）：`Intent.getIntArrayExtra("chars")` 为 **Unicode 码点数组**，`new String(chars, 0, chars.length)` 得到字符串后 `commitText`。适合 emoji 等增补平面字符（如 😸 → `[128568]`）。
  - **ADB_INPUT_CODE** / **ADB_EDITOR_CODE** / **ADB_CLEAR_TEXT** 等：按键码、EditorAction、清空等。

因此：**中文与表情符号** 在 Fastbot3 侧应优先通过 **ADB_INPUT_B64**（或 ADB_INPUT_CHARS）发送，以保证与 adb 脚本、高版本系统一致且不依赖 shell 编码。

### 8.2 Fastbot3 当前实现（AndroidDevice + MonkeyIMEEvent）

- **启用 IME**：`AndroidDevice.enableADBKeyboard()` 在首次 `getInputMethodManager()` 时检查已启用输入法列表中是否包含配置的 ADBKeyBoard ID（如 `com.android.adbkeyboard/.AdbIME`），并设置 `useADBKeyboard`。
- **发送文本**：`MonkeyIMEEvent.injectEvent()` 在检测到软键盘可见后调用 `AndroidDevice.sendText(text)`。
- **sendText(String text)**（已增强）：
  - 若 `text` 中含有 **任意非 ASCII 字符**（`char > 127`），则改为调用 **sendTextViaB64(text)**：将字符串按 **UTF-8** 编码成字节，再 **Base64** 编码，通过 **ADB_INPUT_B64** 广播；ADBKeyBoard 端 Base64 解码后以 UTF-8 解码得到原文，再 `commitText`。这样 **中文、emoji、西文带调** 等均可正确输入。
  - 若 `text` 为纯 ASCII，则仍使用 **ADB_INPUT_TEXT** 直接传 `msg`，与原有行为一致。
- **sendChars(int[] codePoints)**：已存在，用于按 **码点** 发送（如 emoji）；ADBKeyBoard 端用 `new String(chars, 0, chars.length)` 得到字符串后 `commitText`。

使用前需在设备上 **安装 ADBKeyBoard**。启用方式二选一：
- **自动**：若 ADBKeyBoard 已安装但未在系统里启用，Fastbot3 在首次使用 IME 时会尝试执行 `ime enable com.android.adbkeyboard/.AdbIME`（与 adb shell 同源），成功后即视为已启用。需当前进程具备执行 `ime` 的权限（例如通过 `adb shell monkey` 运行时一般具备）。
- **手动**：系统设置 → 语言与输入法 → 虚拟键盘 → 管理键盘 → 勾选 ADBKeyBoard；或执行 `adb shell ime enable com.android.adbkeyboard/.AdbIME`。

在需要输入时还需将当前 IME 切到 ADBKeyBoard；Fastbot3 在发送前会 `checkAndSetInputMethod()` 调用 `InputUtils.switchToIme()`，内部会执行 `ime enable` + `ime set`，自动切换过去。

### 8.3 小结

| 场景           | Fastbot3 做法                    | 说明 |
|----------------|-----------------------------------|------|
| 纯 ASCII       | `sendText(text)` → ADB_INPUT_TEXT | 与原有逻辑一致 |
| 中文/带调/emoji| `sendText(text)` → sendTextViaB64 → ADB_INPUT_B64 | 自动检测非 ASCII，Base64(UTF-8) 可靠传递 |
| 仅码点（如仅 emoji） | `sendChars(int[] codePoints)` → ADB_INPUT_CHARS | 可选，B64 已覆盖 |

这样 Fastbot3 在 **不依赖 scrcpy UHID、不依赖设备物理键盘布局** 的前提下，通过 **ADBKeyBoard + ADB_INPUT_B64** 支持中文、表情符号等任意 Unicode 输入，适合自动化测试中的中文输入场景。

以上分析基于当前阅读的 scrcpy 代码与文档，以及 [senzhk/ADBKeyBoard](https://github.com/senzhk/ADBKeyBoard) 的 README 与 AdbIME 源码，可直接用于对比 Fastbot 等方案的文本注入设计（例如是否使用 IME、是否按码点处理、是否支持 UHID 等）。
