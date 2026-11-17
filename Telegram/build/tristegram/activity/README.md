Tristegram activity server, a Vercel project.

`POST /api/activity` with `{ id, key, kind, title, subtitle, app }` sets the
status of a user (`kind`: music, video, coding, game; anything else clears
it). `GET /api/activity?ids=1,2,3` returns the statuses that are still fresh.
