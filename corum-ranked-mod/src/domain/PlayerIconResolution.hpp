#pragma once

namespace corum::ranked {

// Pure state used by the GD-backed icon component. A card can render its safe
// fallback immediately, then replace it only with cosmetics for the account it
// was created for when Geometry Dash finishes the asynchronous profile lookup.
class PlayerIconResolution final {
public:
    explicit PlayerIconResolution(int expectedAccountId)
      : m_expectedAccountId(expectedAccountId) {}

    [[nodiscard]] int expectedAccountId() const { return m_expectedAccountId; }
    [[nodiscard]] bool hasRemoteIdentity() const { return m_expectedAccountId > 0; }
    [[nodiscard]] bool resolved() const { return m_resolved; }

    bool accept(int accountId) {
        if (!hasRemoteIdentity() || accountId != m_expectedAccountId) return false;
        m_resolved = true;
        return true;
    }

private:
    int m_expectedAccountId = 0;
    bool m_resolved = false;
};

} // namespace corum::ranked
