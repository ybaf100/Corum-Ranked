import { isIP } from "node:net";

/**
 * Header the production Caddy edge sets from its own view of the client: the
 * Cloudflare CF-Connecting-IP value for connections from Cloudflare's ranges,
 * the TCP peer otherwise. Caddy overwrites any client-supplied copy.
 */
export const CLIENT_IP_HEADER = "x-corum-client-ip";

export interface ClientAddressSource {
  readonly headers?: Readonly<Record<string, string | string[] | undefined>>;
  readonly socket?: { readonly remoteAddress?: string | undefined } | null;
}

const unmapped = (address: string): string => address.startsWith("::ffff:") && isIP(address.slice(7)) === 4 ? address.slice(7) : address;

/**
 * The container port is published only on the host's loopback interface, so
 * every peer it sees is a host process (Caddy) arriving through loopback or
 * Docker's private bridge. The edge header is trusted only from such peers.
 */
export const isLocalPeer = (address: string | undefined): boolean => {
  if (!address) return false;
  const value = unmapped(address);
  if (value === "::1" || value.startsWith("fe80:") || /^f[cd][0-9a-f]{2}:/i.test(value)) return true;
  if (isIP(value) !== 4) return false;
  const [a, b] = value.split(".").map(Number) as [number, number];
  return a === 127 || a === 10 || (a === 172 && b >= 16 && b <= 31) || (a === 192 && b === 168);
};

export const clientAddress = (request: ClientAddressSource): string => {
  const peer = request.socket?.remoteAddress;
  if (isLocalPeer(peer)) {
    const raw = request.headers?.[CLIENT_IP_HEADER];
    const header = (Array.isArray(raw) ? raw[0] : raw)?.trim();
    if (header && isIP(header)) return unmapped(header);
  }
  return peer ? unmapped(peer) : "unknown";
};
