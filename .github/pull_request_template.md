## Summary

- What changed:
- Why:

## Validation

- [ ] Flashed from VS Code Pico extension
- [ ] Checked `http://merlinccu/preview`
- [ ] Checked physical panel behaviour
- [ ] Captured evidence for any failures (preview screenshot, panel photo, serial log)
- [ ] Updated display regression assets when UI output changed (`tools/framebuffer_suite.json`, affected `baselines/*.pbm`, and `docs/display-test-checklist.md` when required)

## Display Consistency (UI/Text)

- [ ] Labels/headings/softkey captions are uppercase
- [ ] Data values are mixed case
- [ ] No clipped text on right edge
- [ ] No overlap between rows, footer text, and softkeys
- [ ] Status rows remain aligned

## Weather/Integration (when touched)

- [ ] Weather table columns align (`Time`, `Temp`, `Wind mph`, `Conditions`)
- [ ] Forecast times are sensible for local time
- [ ] Sunrise/sunset shown when provider data exists
- [ ] Source footer is visible/readable
- [ ] HA/WX/MQTT status rows show expected states

## Notes

- Risks:
- Follow-up:
