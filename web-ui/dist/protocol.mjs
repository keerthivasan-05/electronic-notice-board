export function encode(sequence, command, args = []) {
  const payload = [sequence, command, ...args].join('|');
  let crc = 0;
  for (const byte of new TextEncoder().encode(payload)) {
    crc ^= byte;
    for (let bit = 0; bit < 8; bit++) {
      crc = ((crc << 1) ^ (crc & 0x80 ? 0x07 : 0)) & 0xff;
    }
  }
  return '@' + payload + '*' + crc.toString(16).padStart(2, '0').toUpperCase();
}

export function inspect(output, sequence) {
  const lines = output.trim().split(/\r?\n/);
  const items = [];
  let terminal;
  for (const line of lines) {
    const fields = line.trim().split('|');
    if (Number(fields[1]) !== sequence) continue;
    if (fields[0] === 'ITEM' && fields.length === 4 && /^[1-9]\d{0,2}$/.test(fields[2]) &&
        Number(fields[2]) <= 255 && /^[1-3]$/.test(fields[3])) {
      items.push('Notice ' + fields[2] + ' · priority ' + fields[3]);
    }
    if (/^(ACK|ERR)$/.test(fields[0]) && fields.length === 3 && fields[2]) {
      terminal = { ok: fields[0] === 'ACK', detail: fields[2] };
    }
  }
  return { terminal, items };
}
