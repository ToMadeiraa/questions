# 60. Расскажите о настройке процесса менеджмента ветвей репозитория.

Под «настройкой менеджмента ветвей» понимают не только выбор branching-модели, но и её техническое закрепление в SCM-платформе (GitHub/GitLab/Bitbucket/Azure DevOps) и в CI/CD: правила защиты, права, обязательные проверки, политику мержа, жизненный цикл ветки и автоматизацию. Процесс должен быть не «договорённостью в вики», а исполняемой конфигурацией, иначе он разваливается при росте команды.

## 1. Что входит в настройку процесса

| Слой | Что настраивается |
|---|---|
| Модель ветвления | trunk-based / GitHub Flow / GitLab Flow / Git Flow, типы ветвей и их назначение |
| Соглашения | именование ветвей, формат коммитов, размер PR, язык описаний |
| Защита в SCM | protected branches / rulesets, запрет push и force-push, обязательные ревью и статусы |
| Права и роли | кто мержит в `main`, кто может обходить защиту, CODEOWNERS |
| CI/CD | какие пайплайны запускаются на ветку/MR/тег, quality gates, окружения |
| Релизы | теги, release-ветки, hotfix, версионирование, changelog |
| Гигиена | автоудаление после мержа, очистка stale-веток, лимит времени жизни |
| Аудит и метрики | audit log, время жизни ветки, lead time, частота мержей |

## 2. Выбор модели ветвления

Модель выбирается под частоту релизов и зрелость CI, а не «потому что модно».

| Модель | Ветви | Когда уместна | Минусы |
|---|---|---|---|
| **Trunk-based** | `main` + короткоживущие feature-ветки (< 1–2 дней) | Continuous Delivery, сильный CI, feature flags | Требует дисциплины и флагов, иначе «сломанный trunk» |
| **GitHub Flow** | `main` + feature-ветки + PR | Веб-сервисы, частые релизы | Нет явного места для поддержки нескольких версий |
| **GitLab Flow** | `main` + environment-ветки (`preprod`, `production`) или release-ветки | Нужны управляемые окружения и cherry-pick | Больше веток — больше рассинхрона |
| **Git Flow** | `main`, `develop`, `feature/*`, `release/*`, `hotfix/*` | Версионируемые продукты, редкие релизы, несколько поддерживаемых версий | Тяжёлый, длинные ветки, конфликты, плохо дружит с CD |

Практика: для сервисов — trunk-based/GitHub Flow; для библиотек и продуктов с релизами — trunk-based + `release/*` при необходимости; Git Flow — только если есть объективная причина (поддержка LTS-версий).

## 3. Соглашения об именовании и коммитах

Именование задаётся шаблоном и проверяется автоматически (GitLab push rules, hooks, CI-линтер):

```
feature/<TICKET>-<short-slug>     feature/PROJ-142-oauth-refresh
bugfix/<TICKET>-<short-slug>      bugfix/PROJ-201-null-user
hotfix/<version>-<slug>           hotfix/1.8.1-token-expiry
release/<semver>                  release/1.9.0
chore/…, docs/…, ci/…
```

Правила: нижний регистр, дефисы, обязательный ID задачи, ветка создаётся от актуального `main`, живёт не дольше N дней, после мержа удаляется.

Коммиты — Conventional Commits (`feat:`, `fix:`, `chore:`), чтобы автоматически генерировать changelog и версию (semantic-release, release-please). Проверка через commitlint в pre-commit/CI.

## 4. Конфигурация защиты в SCM

Ключевой элемент — объявление «ствола» и релизных веток защищёнными, а всё остальное — рабочими ветками.

**Обязательный набор для `main`:**

- запрет прямого push (только через PR/MR);
- запрет force-push и удаления ветки;
- минимум 1–2 approvals, approval от CODEOWNERS затронутых областей;
- сброс approvals при новых коммитах (`dismiss stale approvals`);
- обязательные status checks (build, tests, lint, security scan) и требование «ветка актуальна относительно `main`»;
- обязательное разрешение всех комментариев;
- подписанные коммиты (если принято);
- linear history, если выбрана стратегия squash/rebase;
- список bypass-акторов — минимальный и под аудитом.

**Уровни настройки:**

- **GitHub** — Rulesets (пришли на смену branch protection rules): правила по маске ветвей, required workflows, merge queue, bypass list. Плюс настройки репозитория: default branch, разрешённые merge-стратегии, auto-delete head branches.
- **GitLab** — Protected branches (`push_access_level`, `merge_access_level`, `allow_force_push`), Merge request approval rules, Merge checks, Merge trains, Push rules.
- **Azure DevOps** — Branch policies: Build validation, Required reviewers, Comment resolution, Merge strategy, Auto-complete.
- **Bitbucket** — Branch permissions, Merge checks, Default reviewers.

Пример: включение защиты `main` в GitHub через `gh`/API (декларативно лучше через Terraform-провайдер `github`):

```bash
gh api --method PUT repos/org/repo/branches/main/protection \
  -f 'required_status_checks[strict]=true' \
  -f 'required_status_checks[contexts][]=ci/test' \
  -f 'enforce_admins=true' \
  -f 'required_pull_request_reviews[required_approving_review_count]=2' \
  -f 'restrictions=null'
```

GitLab (protected branch + запрет force-push):

```bash
curl -X POST -H "PRIVATE-TOKEN: $TOKEN" \
  "https://gitlab.example.com/api/v4/projects/$ID/protected_branches" \
  --data "name=main&push_access_level=0&merge_access_level=30&allow_force_push=false"
```

Terraform для повторяемости на десятках репозиториев:

```hcl
resource "github_branch_protection" "main" {
  repository_id = github_repository.repo.node_id
  pattern       = "main"
  enforce_admins = true

  required_pull_request_reviews {
    required_approving_review_count = 2
    dismiss_stale_reviews           = true
    require_code_owner_reviews      = true
  }
  required_status_checks {
    strict   = true
    contexts = ["ci/test", "ci/security"]
  }
  allows_force_pushes = false
  allows_deletions    = false
}
```

**CODEOWNERS** закрепляет ответственность за пути и автоматически назначает ревьюеров:

```
/backend/api/    @team-backend
/infra/          @team-sre
*.sql            @team-dba
```

## 5. Стратегия мержа

| Стратегия | История | Когда |
|---|---|---|
| Merge commit | Сохраняет топологию, читаемо для крупных фич | Нужна трассировка ветки |
| Squash | Линейная, один коммит на PR | Trunk-based, мелкие PR — дефолт |
| Rebase + merge | Линейная, сохраняет атомарные коммиты | Команда умеет rebase |
| Fast-forward only | Полностью линейная | Жёсткая дисциплина |

Обычно фиксируют **одну** разрешённую стратегию на репозиторий, чтобы история была предсказуемой; при squash — требование к заголовку PR (он становится сообщением коммита).

Для загруженного `main` включают **merge queue / merge trains**: PR проверяются в очереди на актуальном состоянии ствола, что убирает «зелёный PR, красный main» и гонки при параллельных мержах.

## 6. Интеграция с CI/CD

Пайплайн привязывается к типу ветки/событию:

**GitHub Actions:**

```yaml
on:
  push:
    branches: [main, 'release/**', 'hotfix/**']
    tags: ['v*']
  pull_request:
    branches: [main]
```

**GitLab CI — правила по типу пайплайна:**

```yaml
workflow:
  rules:
    - if: '$CI_PIPELINE_SOURCE == "merge_request_event"'   # проверки MR
    - if: '$CI_COMMIT_BRANCH == $CI_DEFAULT_BRANCH'         # сборка/деплой ствола
    - if: '$CI_COMMIT_TAG'                                  # релизный пайплайн
```

Типовой маппинг:

| Ветка/событие | Пайплайн | Результат |
|---|---|---|
| feature/PR | build + unit + lint + SAST, ephemeral-окружение | quality gate, preview |
| `main` | полный CI + deploy в dev/stage | артефакт, nightly |
| `release/*` | регресс, сборка релизного кандидата | RC, staging |
| тег `v*` | сборка + публикация + deploy prod (с approval) | релиз |
| `hotfix/*` | ускоренный CI + deploy prod по approval | патч |

Ветка не должна попадать в прод напрямую: деплой в prod — только с тега или через environment с manual approval. В GitLab для этого `environment` + `when: manual`, в GitHub — Environments с required reviewers.

## 7. Релизный процесс: теги, release-ветки, hotfix

- **Теги** — аннотированные, подписанные, формат SemVer (`v1.9.0`), создаются из `main` или `release/*`, неизменяемы (защита тегов от перезаписи).
- **Release-ветка** нужна, если параллельно поддерживаются версии: `release/1.9` отрезается на feature freeze, в неё идут только исправления, обратно в `main` — merge/cherry-pick.
- **Hotfix**: `hotfix/1.9.1-<slug>` от тега/`release/1.9` → PR в release-ветку и обязательный обратный мерж в `main`, иначе исправление «потеряется» в следующем релизе. Это правило стоит закрепить чек-листом в шаблоне PR.
- **Changelog и версия** генерируются автоматически из коммитов (semantic-release / release-please), тег ставится пайплайном, а не руками.

## 8. Автоматизация и гигиена

- автоудаление head-ветки после мержа (настройка репозитория);
- scheduled job, который находит ветки старше N дней / без активности и уведомляет владельца, затем удаляет;
- защита от stale-PR: напоминания ревьюерам, SLA на ревью (например, 1 рабочий день);
- линтер имён ветвей и commitlint в pre-commit + CI;
- policy as code: защита ветвей и правила репозитория в Terraform/Crossplane, проверка дрейфа в CI;
- аудит: включённый audit log, отчёт «кто обходил защиту».

## 9. Метрики процесса

- время жизни ветки/PR (branch age, lead time for changes);
- время до первого ревью и до мержа;
- размер PR (строк, файлов) — цель < 400 строк;
- частота мержей в `main` (не реже раза в день на разработчика в trunk-based);
- DORA: deployment frequency, lead time, change failure rate, MTTR;
- доля PR, прошедших без обхода защиты.

## 10. Пошаговый план внедрения

1. Выбрать модель под релизный цикл и зафиксировать в `CONTRIBUTING.md`/`BRANCHING.md`.
2. Определить типы ветвей, шаблоны имён, формат коммитов, стратегию мержа.
3. Настроить default branch и защиту `main` + релизных ветвей и тегов.
4. Подключить CODEOWNERS, обязательные ревью и status checks.
5. Описать пайплайны для PR, `main`, `release/*`, тегов; ввести окружения и approvals.
6. Ввести release-процесс: SemVer-теги, автогенерация changelog, регламент hotfix и обратных мержей.
7. Включить автоудаление веток и джоб очистки stale-веток.
8. Перенести настройки в IaC, добавить аудит и дашборд метрик.
9. Провести обучение и включить правила постепенно (сначала advisory, затем blocking).

## 11. Антипаттерны

- долгоживущие feature-ветки и «big bang» мержи;
- прямой push в `main` «только для меня»;
- защита настроена, но `enforce_admins` выключен — правила обходятся;
- несколько разрешённых стратегий мержа без регламента;
- hotfix без обратного мержа в `main`;
- настройка руками в UI без версионирования — при переносе репозитория всё теряется;
- релизы «с ноутбука» мимо пайплайна, теги без защиты.

**Коротко:** процесс менеджмента ветвей = выбранная модель + жёстко закодированные в SCM правила (protected branches, approvals, checks, merge strategy) + пайплайны, привязанные к типам веток и тегам + регламент релизов/hotfix + автоматическая гигиена и метрики. Ключевой критерий качества — правила исполняются платформой, а не памятью разработчиков.