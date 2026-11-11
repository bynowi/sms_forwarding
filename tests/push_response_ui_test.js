// 执行实际页面脚本，验证响应规则回填、转义及通道类型切换。
const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const path = require('node:path');
const elements = {};
const input = () => ({value: '', style: {}, dataset: {}, innerHTML: '', textContent: '', placeholder: ''});
const context = {
  console, Date, Promise, URLSearchParams,
  document: {getElementById: id => elements[id] || null, querySelector: () => null, querySelectorAll: () => [], addEventListener() {}, createElement: input},
  addEventListener() {}, setTimeout() {}, clearTimeout() {}, setInterval() {}, clearInterval() {},
};
context.window = context;
vm.createContext(context);
vm.runInContext(fs.readFileSync(path.join(__dirname, '../code/web_src/app.js'), 'utf8'), context);
let html = context.buildPushChannels([{type: 7, successRule: 'data.status="ok"'}]);
assert(html.includes('name="push0rule"'));
assert(html.includes('value="data.status=&quot;ok&quot;"'));
html = context.buildPushChannels([{successRule: 'code="<script>"'}]);
assert(!html.includes('<script>'));
assert(html.includes('&lt;script&gt;'));
html = context.buildPushChannels([{}]);
assert(html.includes('id="responseRule0" maxlength="160" value=""'));
for (const id of ['push0type','hint0','extra0','custom0','responseRuleGroup0','key10','key20',
                  'key1label0','key2label0','urllabel0','url0','chsum0']) elements[id] = input();
for (const type of [1,3,7,4,5,6,8,9,10]) {
  elements.push0type.value = String(type);
  context.updateTypeHint(0);
  assert.equal(elements.responseRuleGroup0.style.display, [1,3,7].includes(type) ? 'block' : 'none');
}
console.log('push response UI regressions passed: saved rules, escaping and channel-specific fields');
