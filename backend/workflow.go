// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

package projects

import "github.com/da0x/uione/one"

// The day projects got workflows. Every project made before then takes the
// delivery workflow its people asked for: triage to accepted, each step taken by
// the roles they named, with its roles named for that workflow and allowing what
// they do in it. Every issue starts over in Triage. It's done once, the first time
// the backend starts with it.
func init() {
	Module.Add(one.Once("2026-10-07 workflows", workflows))
}

func workflows(s *one.System) error {
	projects, err := one.FetchAll[Project](s)
	if err != nil {
		return err
	}
	for _, p := range projects {
		if err := workflow(s, p.ID); err != nil {
			return err
		}
	}
	return nil
}

func workflow(s *one.System, project string) error {
	role := func(name string) string { return one.Key(project, name) }
	phase := func(name string) string { return one.Key(project, name) }

	// Its roles: the old ones renamed for the workflow, allowing what they do now.
	works := []any{"issue::create", "issue::update", "issue::move", "comment::create"}
	roles := []struct {
		name, title string
		may         []any
	}{
		{"maintainer", "Maintainer", []any{
			"project::update", "member::create", "member::delete", "role::create", "role::update",
			"phase::create", "phase::update", "step::create", "step::delete",
			"issue::create", "issue::update", "issue::move", "comment::create",
			"report::resolve", "reply::create",
		}},
		{"developer", "Programmer", works},
		{"tester", "Quality engineer", works},
		{"approver", "Product owner", works},
		{"reporter", "Reporter", []any{"issue::create", "comment::create"}},
	}
	for _, r := range roles {
		if existing, err := one.Fetch[Role](s, role(r.name)); err != nil {
			return err
		} else if existing == nil {
			continue
		}
		if _, err := s.Run("projects::role::update", map[string]any{"id": role(r.name), "title": r.title, "may": r.may}); err != nil {
			return err
		}
	}
	if _, err := s.Run("projects::role::create", map[string]any{"project": project, "name": "member", "title": "Member", "may": works}); err != nil {
		return err
	}

	// Its phases, in order.
	phases := []struct{ name, title string }{
		{"triage", "Triage"}, {"ready", "Ready"}, {"in_progress", "In progress"},
		{"in_review", "In review"}, {"verified", "Verified"}, {"accepted", "Accepted"},
	}
	for i, p := range phases {
		if _, err := s.Run("projects::phase::create", map[string]any{"project": project, "name": p.name, "title": p.title, "position": float64(i + 1)}); err != nil {
			return err
		}
	}

	// Its steps, and who takes each: a maintainer may take any of them.
	steps := []struct {
		from, to string
		roles    []string
	}{
		{"triage", "ready", []string{"approver"}},
		{"ready", "in_progress", []string{"developer"}},
		{"in_progress", "in_review", []string{"developer"}},
		{"in_review", "verified", []string{"tester"}},
		{"in_review", "in_progress", []string{"tester", "developer"}},
		{"verified", "accepted", []string{"approver"}},
	}
	for _, step := range steps {
		taken := []any{role("maintainer")}
		for _, r := range step.roles {
			taken = append(taken, role(r))
		}
		if _, err := s.Run("projects::step::create", map[string]any{"project": project, "from": phase(step.from), "to": phase(step.to), "roles": taken}); err != nil {
			return err
		}
	}

	// New issues start in Triage, and so does every issue there is.
	if _, err := s.Run("projects::project::update", map[string]any{"id": project, "start": phase("triage")}); err != nil {
		return err
	}
	issues, err := one.FetchWhere[Issue](s, "project", project)
	if err != nil {
		return err
	}
	for _, issue := range issues {
		if _, err := s.Run("projects::issue::update", map[string]any{"id": issue.ID, "phase": phase("triage")}); err != nil {
			return err
		}
	}
	return nil
}
