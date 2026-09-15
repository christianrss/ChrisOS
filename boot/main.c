kstart
  bootinfo/gdt/idt/pic
  pit_init(60) / ps2_init
  pmm/mm/heap
  gfx_init
  desktop_init
  sti
  desktop_run
    desktop_frame
      TASKBAR_EDITOR -> editor_window_open
      task_run_all -> editor_run
        ui_window
        input_next_event -> ed_handle
        desenha linhas visuais + caret + status
    gfx_present
