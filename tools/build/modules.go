// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// found in the LICENSE file.

package build

import (
	"fmt"
	"path/filepath"
	"strings"
)

const (
	// ImageModuleName is the name of the build API module of images.
	imageModuleName = "images.json"
	// PlatformModuleName is the the name of the build API module of valid
	// platforms that tests can target.
	platformModuleName = "platforms.json"
	// TestModuleName is the name of the build API module of tests.
	testModuleName = "tests.json"
)

// Modules is a convenience interface for accessing the various build API
// modules associated with a build.
type Modules struct {
	buildDir  string
	images    []Image
	platforms []DimensionSet
	testSpecs []TestSpec
}

// NewModules returns a Modules associated with a given build directory.
func NewModules(buildDir string) (*Modules, error) {
	var errMsgs []string
	var err error
	m := &Modules{buildDir: buildDir}

	m.images, err = LoadImages(m.ImageManifest())
	if err != nil {
		errMsgs = append(errMsgs, err.Error())
	}

	m.platforms, err = LoadPlatforms(m.PlatformManifest())
	if err != nil {
		errMsgs = append(errMsgs, err.Error())
	}

	m.testSpecs, err = LoadTestSpecs(m.TestManifest())
	if err != nil {
		errMsgs = append(errMsgs, err.Error())
	}

	if len(errMsgs) > 0 {
		return nil, fmt.Errorf(strings.Join(errMsgs, "\n"))
	}
	return m, nil
}

// BuildDir returns the fuchsia build directory root.
func (m Modules) BuildDir() string {
	return m.buildDir
}

// Images returns the aggregated build APIs of fuchsia and zircon images.
func (m Modules) Images() []Image {
	return m.images
}

// ImageManifest returns the path to the manifest of images in the build.
func (m Modules) ImageManifest() string {
	return filepath.Join(m.BuildDir(), imageModuleName)
}

// Platforms returns the build API module of available platforms to test on.
func (m Modules) Platforms() []DimensionSet {
	return m.platforms
}

// PlatformManifest returns the path to the manifest of available test platforms.
func (m Modules) PlatformManifest() string {
	return filepath.Join(m.BuildDir(), platformModuleName)
}

// TestSpecs returns the build API module of tests.
func (m Modules) TestSpecs() []TestSpec {
	return m.testSpecs
}

// TestManifest returns the path to the manifest of tests in the build.
func (m Modules) TestManifest() string {
	return filepath.Join(m.BuildDir(), testModuleName)
}
