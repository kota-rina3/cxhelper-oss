// test.js — cxhelper-oss 与闭源版对照测试
// 用法：node test.js [闭源.node路径]
//       或 ELECTRON_RUN_AS_NODE=1 ./cxstudy test.js <闭源路径>
const path = require("path");
const fs = require("fs");

const OSS = require(path.join(__dirname, "../build/CxHelperExt.node"));
const CLOSED_PATH =
  process.argv[2] ||
  "/home/ricer/Desktop/XXT64/resources/app/node_modules/cxhelper/native/linux-x64/CxHelperExt.node";

let pass = 0, fail = 0;
function check(name, cond, detail) {
  if (cond) { pass++; console.log("  ✅", name); }
  else { fail++; console.log("  ❌", name, detail || ""); }
}

console.log("== 1. 模块导出 ==");
check("导出三个函数",
  typeof OSS.GetDesktopWindowInfo === "function" &&
  typeof OSS.GetClipboardFilePath === "function" &&
  typeof OSS.SetClipboardFilePath === "function",
  Object.keys(OSS));

console.log("== 2. GetDesktopWindowInfo ==");
const info = OSS.GetDesktopWindowInfo();
console.log("  返回类型:", typeof info, "| 前 80 字符:", String(info).slice(0, 80));
let winArr = null;
try { winArr = JSON.parse(info); } catch {}
check("JSON 可解析", Array.isArray(winArr), String(info).slice(0, 60));
if (Array.isArray(winArr)) {
  const sample = winArr[0];
  if (sample) {
    check("字段完整 (winId/title/x/y/width/height/level)",
      ["winId", "title", "x", "y", "width", "height", "level"]
        .every(k => k in sample), JSON.stringify(sample));
    check("winId 为正整数", Number.isInteger(sample.winId) && sample.winId > 0);
    check("title 非空", typeof sample.title === "string" && sample.title.length > 0);
  }
  // 用 xdotool 交叉验证至少一个窗口
  try {
    const { execSync } = require("child_process");
    const dpyWinIds = execSync(
      `xdotool search --onlyvisible --name . 2>/dev/null || true`)
      .toString().split("\n").filter(Boolean).map(Number);
    const ourIds = winArr.map(w => Number(w.winId));
    const hit = dpyWinIds.some(id => ourIds.includes(id));
    console.log(`  xdotool 可见窗口 ${dpyWinIds.length} 个, 交集命中:`, hit);
    check("与 xdotool 窗口列表有交集", hit || dpyWinIds.length === 0);
  } catch {}
}

console.log("== 3. 剪贴板 ==");
const got = OSS.GetClipboardFilePath();
console.log("  Get 返回:", String(got).slice(0, 100));
let arr = null;
try { arr = JSON.parse(got); } catch {}
check("Get 返回 JSON 数组字符串", Array.isArray(arr), String(got).slice(0, 60));

check("Set(数组) 返回 true",
  OSS.SetClipboardFilePath(["/tmp/cxoss test 1.txt", "/tmp/cxoss2.txt"]) === true);
check("Set(非数组) 抛异常", (() => {
  try { OSS.SetClipboardFilePath("/tmp/x"); return false; }
  catch (e) { return /Array of file paths expected/.test(e.message); }
})());
check("Set(空数组) 返回 false", OSS.SetClipboardFilePath([]) === false);

console.log("== 4. 与闭源版对照 ==");
if (fs.existsSync(CLOSED_PATH)) {
  const CLOSED = require(CLOSED_PATH);
  const ci = CLOSED.GetDesktopWindowInfo();
  const oi = OSS.GetDesktopWindowInfo();
  check("两者返回类型一致", typeof ci === typeof oi,
    `闭源=${typeof ci} 开源=${typeof oi}`);
  // GetClipboardFilePath 受实时剪贴板状态影响，只打印对比
  console.log("  闭源 Get:", String(CLOSED.GetClipboardFilePath()).slice(0, 80));
  console.log("  开源 Get:", String(OSS.GetClipboardFilePath()).slice(0, 80));
  // 互通验证：开源 Set 之后，闭源 Get 应能读到其中的路径
  OSS.SetClipboardFilePath(["/tmp/cxinterop_a.txt"]);
  const back = String(CLOSED.GetClipboardFilePath());
  check("跨实现互通（闭源能读到开源 Set 的路径）",
    back.includes("/tmp/cxinterop_a.txt"), back.slice(0, 80));
} else {
  console.log("  (未找到闭源版，跳过)");
}

console.log(`\n结果: PASS ${pass} / FAIL ${fail}`);
process.exit(fail ? 1 : 0);
