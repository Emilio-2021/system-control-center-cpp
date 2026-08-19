# C++ Project Plan

## Goal

Rebuild the System Control Center as a C++ web application while preserving the useful ERP workflow and demonstrating production-minded C++ skills.

The Python project at `C:\Python\SystemControlCenter` remains independent and untouched.

## Milestones

### 1. Foundation — complete

- CMake and MinGW C++17 build
- Thread-safe inventory domain
- Initial unit test
- Separate SQLite database snapshot
- Existing HTML templates and Bootstrap assets copied for reuse

### 2. Web server

- Add a C++ HTTP server dependency or implementation
- [x] Serve `/static/*` and HTML templates
- [x] Add a health-check route
- [x] Document build and run commands
- [x] Add request routing for login and application workflows

### 3. Persistence

- [x] Add SQLite access behind a small repository layer
- [x] Read products and users from `data/system_control_center.db`
- [x] Use prepared statements and explicit transactions
- [x] Keep database connection/session ownership scoped to requests

### 4. Authentication and UI

- [x] Implement login and logout routes
- [x] Add server-side sessions and password verification
- [x] Reconnect the copied login, dashboard, products, and users templates
- [x] Enforce administrator, operator, and viewer permissions

### 5. Business workflows

- [x] Products and inventory management
- [x] Customer/entity management
- [x] Checkout and order history
- [x] Full refunds with inventory restoration

### 6. Concurrency and quality

- Add concurrent checkout tests against limited stock
- Verify that inventory cannot be oversold
- Test duplicate refund protection
- Add error handling, structured logging, and input validation
- [x] Add authenticated integration coverage for CRUD and invoice workflows
- Add a recruiter-focused architecture and testing section to the README

## Fresh-shell starting point

```powershell
Set-Location C:\CodeBlocks\SystemControlCenterCpp
cmake --build build
ctest --test-dir build --output-on-failure
```

The current demo executable is `system_control_center.exe`.
