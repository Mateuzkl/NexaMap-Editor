# Spawn / NPC Converter manual checks

These checks cover behavior that requires the wxWidgets UI or runtime diagnostics and therefore complements `spawn_xml_converter_tests`.

1. Open the converter from **Tools > Spawn / NPC Converter** and from the welcome screen; confirm both open the same two-tab dialog.
2. Select a singular and plural Canary monster filename; confirm companion and `*-spawn.xml` paths update, while a manually edited destination remains unchanged.
3. Select `spawn.xml` and `MapName-spawn.xml`; confirm the default singular outputs and the plural monster checkbox behavior.
4. With an existing output, choose **No** in overwrite confirmation and verify its contents and timestamp remain unchanged; repeat with **Yes** and verify conversion completes.
5. Cancel the progress dialog before commit and verify no final, `.rme-stage.*`, or `.rme-backup.*` files remain.
6. Repeat the dialog and progress workflow with System, Dark, and Light themes.
7. Repeat at 100%, 125%, and 150% display scaling; verify labels, pickers, tabs, report, and buttons remain visible and usable.
8. Run repeated forward/reverse conversions under the project's memory diagnostics and verify there is no increasing allocation count or handle leak.
9. Confirm the final report shows direction, sources, outputs, area/creature counts, preserved extra attributes, warnings, validation status, and elapsed time without listing every creature.
10. Confirm opening or converting spawn XML does not open, save, or modify an OTBM map and does not invoke the Map ID Converter.
