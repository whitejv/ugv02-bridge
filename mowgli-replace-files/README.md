# Mowgli launch file replacements

Patched copies of MowgliNext bringup launch files for the UGV02 cutover.

Changes vs baseline (`mowgli-orig-files/`):

- Add launch arg `use_hardware_bridge` (default `true`)
- Gate stock `hardware_bridge_node` with `IfCondition(use_hardware_bridge)`
- Forward the arg from `full_system.launch.py` into `mowgli.launch.py`

Apply into `../mowglinext`:

```bash
./scripts/apply-mowgli-replace-files.sh --dry-run
./scripts/apply-mowgli-replace-files.sh
```

This directory is the source of truth for the patches; `../mowglinext` is only
updated when you run the script.
