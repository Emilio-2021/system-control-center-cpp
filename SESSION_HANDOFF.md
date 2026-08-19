# Session Handoff

Last updated: 2026-08-19

## Objective

This project is a C++17 web application modeled after the existing Python System Control Center application at `C:\Python\SystemControlCenter`. The Python project is reference-only and has not been modified.

## Current state

The C++ application builds a native Winsock HTTP server and uses:

- SQLite for persistence.
- bcrypt-compatible password verification and password generation.
- Inja for server-side HTML template rendering.
- nlohmann/json for template data.
- Bootstrap assets in `static/`.
- The Python reference screenshots preserved in `res/`.
- In-memory opaque server-side sessions stored in the running process.

The final runtime layout is at the project root:

```text
C:\CodeBlocks\SystemControlCenterCpp\
  system_control_center.exe
  templates\
  static\
  data\system_control_center.db
  res\
  third_party\
```

`build/` contains only CMake build artifacts. Executables, databases, and logs are ignored by Git.

## Build and test

Run these commands from `C:\CodeBlocks\SystemControlCenterCpp`:

```powershell
cmake -S . -B build -G "MinGW Makefiles"
cmake --build build --config Release
ctest --test-dir build --output-on-failure
```

The current test result is 2/2 passing:

- `inventory_tests`
- `bcrypt_tests`

Start the application with:

```powershell
.\system_control_center.exe
```

Then browse to `http://127.0.0.1:8080`.

## Implemented routes

- `GET /` — Inja-rendered login page.
- `POST /login` — bcrypt password verification and session creation.
- `GET /logout` — session removal and cookie expiration.
- `GET /health` — health response.
- `GET /dashboard` — role-aware dashboard data from SQLite.
- `GET /products-view` — product catalog.
- `POST /products/create`, `/products/update`, `/products/delete/{id}` — administrator product management.
- `GET /entities-view` — person/company registry.
- `POST /entities/create`, `/entities/update`, `/entities/delete/{id}` — administrator entity management.
- `GET /users-view` — administrator user registry.
- `POST /users/create`, `/users/update`, `/users/delete/{id}` — administrator user management with bcrypt hashes.
- `GET /checkout` — operator/administrator checkout page.
- `POST /checkout/create` — transactional multi-line checkout and stock deduction.
- `GET /orders-view` — order history.
- `GET /orders/{id}` — order detail.
- `POST /orders/{id}/refund` — operator/administrator full refund, inventory restoration, and `REFUNDED` status.
- `GET /static/*` and `GET /templates/*` — development asset/template serving.

## Important implementation details

- The Windows system header `bcrypt.h` conflicts with the bundled bcrypt source, so the local public header is named `third_party/bcrypt/scc_bcrypt.h`.
- Inja is included as `third_party/inja/inja.hpp`; JSON is at `third_party/json/nlohmann/json.hpp`.
- The server resolves templates, static files, and the database relative to the executable/current project root.
- Login failures redirect to the login page with an error query parameter.
- Checkout and refund operations use SQLite transactions.
- Refund tables are expected in the database: `order_refunds` and `order_refund_items`.
- The server binds to loopback (`127.0.0.1`) by default.

## Git state

The local repository is initialized on branch `master`.

Previous commit:

```text
b92f236 Create C++ system control center web app
```

## Recommended next steps

1. Add authenticated integration tests for login, dashboard, checkout, order detail, and role restrictions.
2. Add a safe test database or transaction rollback test for multi-line checkout and refunds.
3. Add concurrency tests proving stock cannot be oversold.
4. Improve user-facing error messages for failed product, entity, user, checkout, and refund operations.
5. Reformat the dense checkout/order functions in `src/http_server.cpp`; the build currently succeeds but emits `-Wmisleading-indentation` warnings for some legacy one-line functions.
6. Consider replacing in-memory sessions with a persistent or signed session strategy before production deployment.
7. Review deployment hardening, including request size limits, cookie security, CSRF protection, and production logging.

## Files to review first tomorrow

- `PROJECT_PLAN.md` — project milestones and scope.
- `README.md` — user-facing build and route documentation.
- `src/http_server.cpp` — HTTP routing, authentication, SQLite workflows, and template rendering.
- `templates/checkout.html` — multi-line checkout UI.
- `templates/order_detail.html` — refund form.
- `CMakeLists.txt` — build targets and bundled dependencies.

