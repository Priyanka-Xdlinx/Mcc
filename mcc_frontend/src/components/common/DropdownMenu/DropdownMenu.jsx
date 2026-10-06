import { useState } from "react";
import { Button, Divider, ListSubheader, Menu, MenuItem } from "@mui/material";

/**
 * A button that opens a menu. `items` is `[{ label, onClick, disabled? }]`;
 * `header` is shown above them, unclickable.
 */
export default function DropdownMenu({ trigger, triggerLabel, header, items }) {
  const [anchor, setAnchor] = useState(null);
  const close = () => setAnchor(null);
  return (
    <>
      <Button
        color="inherit"
        size="small"
        aria-haspopup="menu"
        aria-expanded={Boolean(anchor)}
        aria-label={triggerLabel}
        onClick={(e) => setAnchor(e.currentTarget)}
        sx={{ gap: 0.75, fontSize: 13, py: 0.25, minWidth: 0 }}
      >
        {trigger}
      </Button>
      <Menu anchorEl={anchor} open={Boolean(anchor)} onClose={close}>
        {header && <ListSubheader sx={{ lineHeight: 1.4, py: 1 }}>{header}</ListSubheader>}
        {header && <Divider />}
        {items.map((it) => (
          <MenuItem
            key={it.label}
            disabled={it.disabled}
            onClick={() => {
              close();
              it.onClick();
            }}
          >
            {it.label}
          </MenuItem>
        ))}
      </Menu>
    </>
  );
}
