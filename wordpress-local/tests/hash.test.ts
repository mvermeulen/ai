import { describe, expect, it } from "vitest";
import { sha256 } from "../src/lib/hash.js";

describe("sha256", () => {
  it("is deterministic", () => {
    expect(sha256("abc")).toBe(sha256("abc"));
  });

  it("changes when input changes", () => {
    expect(sha256("abc")).not.toBe(sha256("abcd"));
  });
});
