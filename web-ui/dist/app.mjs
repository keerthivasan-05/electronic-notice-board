import { encode, inspect } from './protocol.mjs';

const byId = id => document.getElementById(id);
const command = byId('command');
const message = byId('message');
const priority = byId('priority');
const noticeId = byId('notice-id');
const ttl = byId('ttl');
const copy = byId('copy');
const result = byId('result');
let sequence = 0;
let frame = '';
let offset = 0;

function updatePreview() {
  const text = message.value;
  byId('count').textContent = text.length + ' / 64';
  byId('lcd-title').textContent = 'ID:' + noticeId.value.padStart(3, '0') + '  PRIO:' + priority.value;
  const cycle = text + '   ';
  byId('lcd-message').textContent = text.length > 16
    ? (cycle + cycle).slice(offset, offset + 16)
    : text;
  byId('scroll').disabled = text.length <= 16;
}

function updateFields() {
  const add = command.value === 'ADD';
  const hasId = add || command.value === 'DELETE';
  byId('id-field').hidden = noticeId.disabled = !hasId;
  byId('priority-field').hidden = priority.disabled = !add;
  byId('ttl-field').hidden = ttl.disabled = !add;
  byId('message-field').hidden = message.disabled = !add;
  byId('preview-title').closest('section').hidden = !add;
  if (add) updatePreview();
}

command.addEventListener('change', updateFields);
for (const field of [message, noticeId, priority]) {
  field.addEventListener('input', () => {
    message.setCustomValidity('');
    offset = 0;
    updatePreview();
  });
}
byId('scroll').addEventListener('click', () => {
  offset = (offset + 1) % (message.value.length + 3);
  updatePreview();
});
byId('composer').addEventListener('submit', event => {
  event.preventDefault();
  const operation = command.value;
  const args = [];
  if (operation === 'ADD') {
    if (/[^ -~]|[|*]/.test(message.value)) {
      message.setCustomValidity('Use printable ASCII and leave out | and *.');
      message.reportValidity();
      return;
    }
    args.push(noticeId.valueAsNumber, priority.value, ttl.valueAsNumber, message.value);
  } else if (operation === 'DELETE') {
    args.push(noticeId.valueAsNumber);
  }
  sequence = sequence % 255 + 1;
  frame = encode(sequence, operation, args);
  byId('frame').textContent = frame;
  byId('copy-status').textContent = '';
  copy.disabled = false;
  result.dataset.state = '';
  result.textContent = 'Waiting for reply to sequence ' + sequence + '.';
});
copy.addEventListener('click', async () => {
  try {
    await navigator.clipboard.writeText(frame);
    byId('copy-status').textContent = 'Copied.';
  } catch {
    const range = document.createRange();
    range.selectNodeContents(byId('frame'));
    const selection = window.getSelection();
    selection.removeAllRanges();
    selection.addRange(range);
    byId('copy-status').textContent = 'Frame selected. Use your copy shortcut.';
  }
});
byId('reply-form').addEventListener('submit', event => {
  event.preventDefault();
  if (!frame) {
    result.textContent = 'Generate a frame first.';
    return;
  }
  const { terminal, items } = inspect(byId('reply').value, sequence);
  if (!terminal) {
    result.dataset.state = 'error';
    result.textContent = 'No ACK or ERR found for sequence ' + sequence + '.';
    return;
  }
  result.dataset.state = terminal.ok ? 'ok' : 'error';
  const summary = (terminal.ok ? 'ACK' : 'ERR') + ' · sequence ' + sequence + ' · ' + terminal.detail;
  result.textContent = [summary, ...items].join('\n');
});
updateFields();
