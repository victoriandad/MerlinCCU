## Summary

- What changed:
- Why:

## Validation

- [ ] Flashed from VS Code Pico extension
- [ ] Checked `http://merlinccu/preview`
- [ ] Checked physical panel behaviour
- [ ] Captured evidence for any failures (preview screenshot, panel photo, serial log)
- [ ] Updated display regression assets when UI output changed (`tools/framebuffer_suite.json`, affected `baselines/*.pbm`, and `docs/display-test-checklist.md` when required)

## Display Consistency (when UI output changed)

Rule numbers refer to `docs/ui_requirements.md`.

- [ ] Captions uppercase, values mixed case, brackets only around a current value (LBL, TXT)
- [ ] Every softkey label fits its cell; nothing cut off by the renderer (LBL-6)
- [ ] Body does not overlap a labelled softkey cell, the header, or the footer (SCR-5, SCR-6)
- [ ] Row 5 follows the rules: R5 `HOME` or confirm, L5 view modifier or decline (NAV-2 to NAV-4)
- [ ] No softkey duplicates `BACK STEP` or pages a list; arrows shown only when the cursor acts (NAV-5, NAV-7, NAV-8)

## New Or Reworked Page

- [ ] Page type chosen (section 2)
- [ ] Each softkey has one role; unused keys are blank (section 3)
- [ ] Body built from shared blocks (section 7)
- [ ] No-data, waiting, stale, and failed states defined (section 8)
- [ ] Golden image added for every state (section 9)

## Weather/Integration (when touched)

- [ ] Weather table columns align (`Time`, `Temp`, `Wind mph`, `Conditions`)
- [ ] Forecast times are sensible for local time
- [ ] Sunrise/sunset shown when provider data exists
- [ ] Source footer is visible/readable
- [ ] HA/WX/MQTT status rows show expected states

## Notes

- Risks:
- Follow-up:
