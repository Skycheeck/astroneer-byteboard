// Little-endian cursor over a Buffer, shared by every binary reader in tools/lib.
// Reads the UE4 primitive encodings: integers, floats, raw bytes and FString (int32 length, then latin1 or,
// for a negative length, UTF-16LE characters, both with a terminating NUL that the length counts).

// The input does not have the binary layout the reader expects (wrong file, other engine version).
export class FormatError extends Error {}

export class Reader {
  constructor(buffer, position = 0) {
    this.buffer = buffer;
    this.position = position;
  }

  seek(position) {
    if (position < 0 || position > this.buffer.length) {
      throw new FormatError(`seek to ${position} outside a ${this.buffer.length}-byte buffer`);
    }
    this.position = position;
  }

  skip(count) {
    this.seek(this.position + count);
  }

  #take(count) {
    const start = this.position;
    if (count < 0 || start + count > this.buffer.length) {
      throw new FormatError(`read of ${count} bytes at ${start} past the end of a ${this.buffer.length}-byte buffer`);
    }
    this.position = start + count;
    return start;
  }

  u8() {
    return this.buffer.readUInt8(this.#take(1));
  }

  i8() {
    return this.buffer.readInt8(this.#take(1));
  }

  i32() {
    return this.buffer.readInt32LE(this.#take(4));
  }

  u32() {
    return this.buffer.readUInt32LE(this.#take(4));
  }

  f32() {
    return this.buffer.readFloatLE(this.#take(4));
  }

  // 64-bit values are offsets and sizes here; anything above 2^53 means the layout was misread.
  i64() {
    return toSafeNumber(this.buffer.readBigInt64LE(this.#take(8)));
  }

  u64() {
    return toSafeNumber(this.buffer.readBigUInt64LE(this.#take(8)));
  }

  bytes(count) {
    const start = this.#take(count);
    return this.buffer.subarray(start, start + count);
  }

  fstring() {
    const length = this.i32();
    if (length === 0) return '';
    const isWide = length < 0;
    const charCount = Math.abs(length);
    const byteCount = isWide ? charCount * 2 : charCount;
    const start = this.#take(byteCount);
    const end = start + byteCount - (isWide ? 2 : 1);
    const terminator = isWide ? this.buffer.readUInt16LE(end) : this.buffer[end];
    if (terminator !== 0) throw new FormatError(`FString at ${start - 4} has no terminating NUL`);
    return this.buffer.toString(isWide ? 'utf16le' : 'latin1', start, end);
  }
}

function toSafeNumber(value) {
  if (value > BigInt(Number.MAX_SAFE_INTEGER) || value < BigInt(Number.MIN_SAFE_INTEGER)) {
    throw new FormatError(`64-bit value ${value} is out of range`);
  }
  return Number(value);
}
