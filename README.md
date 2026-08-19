# System Control Center C++

A C++17 version of the System Control Center showcase project.

The Python application remains separate at `C:\Python\SystemControlCenter` and is not modified by this project.

## Current milestone

The current version contains a native C++ web server, Inja-rendered HTML templates, SQLite persistence, bcrypt-compatible authentication, server-side sessions, role-based access control, and the main ERP workflows.

## Build with CMake and MinGW

From PowerShell:

```powershell
cmake -S . -B build -G "MinGW Makefiles"
cmake --build build
ctest --test-dir build --output-on-failure
```

Run the demo:

```powershell
system_control_center.exe
```

The web server listens on `http://127.0.0.1:8080`. The project root contains the self-contained runtime layout:

```text
SystemControlCenterCpp/
  system_control_center.exe
  templates/
  static/
  data/
  res/
```

Pass a different port as the first argument when needed:

```powershell
system_control_center.exe 8081
```

Available foundation routes:

- `GET /` serves `templates/login.html`
- `GET /health` returns a JSON health response
- `POST /login` verifies credentials against `data/system_control_center.db`
- `GET /products-view` renders the SQLite-backed product catalog
- `GET /entities-view` renders the SQLite-backed person/company registry
- `GET /users-view` renders the administrator-only user and role registry
- `GET /checkout` renders the checkout form for operators and administrators
- `POST /checkout/create` creates a completed order and deducts stock transactionally
- `GET /orders-view` renders order history
- `GET /orders/{order_id}` renders order details
- `POST /orders/{order_id}/refund` records a full refund, restores inventory, and marks the order `REFUNDED`
- `GET /static/*` serves Bootstrap assets
- `GET /templates/*` serves the copied HTML templates

## Remaining hardening and test work

- Add integration tests for authenticated routes and multi-line checkout
- Add concurrency tests proving inventory cannot be oversold
- Improve user-facing error messages and deployment hardening
