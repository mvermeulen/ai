import { describe, expect, it } from "vitest";
import { assertCanApply } from "../src/config.js";

describe("assertCanApply", () => {
  it("allows expected host in allowlist", () => {
    expect(() => assertCanApply("example.com", ["example.com"], "example.com")).not.toThrow();
  });

  it("throws when host is not allowlisted", () => {
    expect(() => assertCanApply("bad.com", ["example.com"], undefined)).toThrow(
      /not in WP_ALLOWED_HOSTS/
    );
  });

  it("throws when expected host mismatches", () => {
    expect(() => assertCanApply("example.com", ["example.com"], "other.com")).toThrow(
      /does not match/
    );
  });
});
