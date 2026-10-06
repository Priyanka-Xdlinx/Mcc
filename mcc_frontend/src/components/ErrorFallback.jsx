import { Button, Stack, Text } from "./common/index.js";

/** Shown when a page crashed while rendering. A second failure offers a full reload. */
export default function ErrorFallback({ onRetry, canReload }) {
  return (
    <Stack align="center" justify="center" gap={2} sx={{ minHeight: "60vh", p: 4, textAlign: "center" }} role="alert">
      <Text variant="heading">Something went wrong on this page</Text>
      <Text muted>The rest of the console is unaffected. Try the page again.</Text>
      <Stack direction="row" gap={1}>
        <Button variant="contained" onClick={onRetry}>
          Try again
        </Button>
        {canReload && <Button onClick={() => window.location.reload()}>Reload the console</Button>}
      </Stack>
    </Stack>
  );
}
