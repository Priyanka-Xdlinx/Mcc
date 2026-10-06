/**
 * The common component kit, and the only way in to it: nothing imports a kit
 * component by its own path.
 *
 * `@mui/material` is imported inside this folder and nowhere else. Feature
 * views that draw instruments (byte maps, display windows, the globe) use
 * their own stylesheets instead; everything else composes from here.
 */

/** Layout */
export { default as Box } from "./Box/index.js";
export { default as Stack } from "./Stack/index.js";
export { default as PageContainer } from "./PageContainer/index.js";

/** Typography and icons */
export { default as Text } from "./Text/index.js";
export * from "./Icons/index.js";

/** Actions and navigation */
export { default as Button } from "./Button/index.js";
export { default as IconButton } from "./IconButton/index.js";
export { default as NavTabs } from "./NavTabs/index.js";
export { default as DropdownMenu } from "./DropdownMenu/index.js";
export { default as SegmentedControl } from "./SegmentedControl/index.js";

/** Form controls */
export { default as TextField } from "./TextField/index.js";
export { default as SelectField } from "./SelectField/index.js";
export { default as CheckboxField } from "./CheckboxField/index.js";

/** Data display */
export * from "./DataTable/index.js";
export { default as StatusChip } from "./StatusChip/index.js";

/** Feedback */
export { default as Dialog } from "./Dialog/index.js";
export { default as EmptyState } from "./EmptyState/index.js";
export { default as ErrorBanner } from "./ErrorBanner/index.js";
export { default as Spinner } from "./Spinner/index.js";
export { default as Toast } from "./Toast/index.js";
