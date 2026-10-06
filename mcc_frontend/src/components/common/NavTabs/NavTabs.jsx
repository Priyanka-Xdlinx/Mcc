import { Tab, Tabs } from "@mui/material";

/**
 * The page tabs. Router-agnostic: `items` is `[{ value, label, icon?, hint? }]`
 * and the caller navigates in `onChange`.
 */
export default function NavTabs({ value, items, onChange, label }) {
  const known = items.some((i) => i.value === value);
  return (
    <Tabs
      value={known ? value : false}
      onChange={(_e, v) => onChange(v)}
      variant="scrollable"
      scrollButtons={false}
      aria-label={label}
      sx={{ minHeight: 40, "& .MuiTabs-indicator": { height: 2 } }}
    >
      {items.map((i) => (
        <Tab
          key={i.value}
          value={i.value}
          label={i.label}
          icon={i.icon}
          iconPosition="start"
          title={i.hint}
          sx={{ minHeight: 40, minWidth: 0, py: 0, px: 1.25, gap: 0.75, fontSize: 13, textTransform: "none", fontWeight: 550, "& .MuiTab-icon": { mr: 0 } }}
        />
      ))}
    </Tabs>
  );
}
