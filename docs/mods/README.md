# Lua mods

You can load source-only Lua packages, add a settings/action page to F1/Esc → Mods, and request one supported native Online-menu entry. Packages run inside a restricted host API. UI events do not grant access to actors, combat, quests, game saves or arbitrary engine functions.

This is an experimental author interface. Linux has automated and coordinated runtime evidence as recorded in the internal candidate report. Native Windows validation remains pending; source portability is not a platform support claim.

## Workflow

Start with [01 · Start Here](01-start-here.md), then use the small examples and reference links. Ordinary scripts need no compiler. Optional native providers use explicitly activated, platform-specific trusted libraries and a separate C ABI.

## Reference

- [Settings API](../lua-mods.md)
- [Actions, status and provider commands](../provider-actions.md)
- [Menu registration v1](03-menu-registration.md)
- [Compatibility, capabilities and troubleshooting](04-compatibility.md)
- [Native provider loading](../native-providers.md)

Internal attribution, diagnostic addresses and test histories belong in the review reports, not mod installation instructions.
