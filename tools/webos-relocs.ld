/* glibc 2.19 merges these tables with BIND_NOW, even if other sections separate them. */
SECTIONS
{
  .rel.dyn : { *(.rel.dyn) }
  .rel.plt : { *(.rel.plt) }
}
INSERT BEFORE .ARM.exidx;
