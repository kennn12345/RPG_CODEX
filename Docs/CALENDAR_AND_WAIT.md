# Calendar and waiting

The calendar uses seven-day weeks, twelve 30-day months, and 360-day years. It starts at Day 1, Month 1, Year 1 at the level's starting sky time. Month and weekday names are numbered placeholders until custom names are chosen. Weekdays continue across month/year boundaries; the last week of a year can be partial.

A complete cycle takes 120 real minutes: approximately one hour of daylight (06:00–18:00) and one hour of night (18:00–06:00), with twilight around the horizon crossings. One game hour takes five real minutes. Configure `RealMinutesPerDay` under `[/Script/RPG_CODEX.RPGCalendarSubsystem]` in `Config/DefaultGame.ini`.

D-Pad Down opens a modal Wait popup; targeting a bed opens Sleep. Left/right or arrow keys select 1–24 game hours. A/Enter confirms, B/Escape cancels. Mouse buttons and slider also work. The popup shows the current date/time/week and arrival date/time/week. Time and player movement pause while choosing; cancellation leaves the clock unchanged. Confirmation advances the calendar and sky together immediately.

Waiting is blocked during airborne movement, flight, blocking, charging, or held action buttons. Existing Wait/Sleep action events are still emitted when the menu opens. This feature advances time; it does not yet simulate offline NPC schedules, healing, or hunger.

`URPGCalendarSubsystem` owns the game-instance clock and survives map travel. `ARPGSkyCycle` registers as its visual receiver. Frozen (`bAnimate=false`) sky previews remain frozen. New Play sessions start a fresh calendar; disk save/load is not implemented. `GetElapsedGameSeconds` / `SetElapsedGameSeconds` provide future save-state hooks. `MonthNames` / `WeekdayNames` support later naming.

`URPGWaitSubsystem` provides the per-local-player Slate popup. No new widget asset or level placement is needed. Restart Unreal after the native build to load the new classes.

Verification: `RPG.Calendar.Arithmetic` covers pace, preview, month/year/week rollover, and rejected skips. `RPG.Calendar.WaitPopup` exercises the enhanced-input Wait action in PIE, Slate gamepad selection/confirm/cancel, pause restoration, bounds, and sky synchronization. It captures `Saved/CalendarWait/WaitPopup.png`.
