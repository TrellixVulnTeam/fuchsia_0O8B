// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![allow(dead_code)]
use {
    failure::{format_err, Error},
    fidl_fuchsia_diagnostics::{self, ComponentSelector, Selector, StringSelector, TreeSelector},
    lazy_static::lazy_static,
    regex::{Regex, RegexSet},
    regex_syntax,
    std::fs,
    std::io::{BufRead, BufReader},
    std::path::{Path, PathBuf},
};
// Character used to delimit the different sections of an inspect selector,
// the component selector, the tree selector, and the property selector.
static SELECTOR_DELIMITER: char = ':';

// Character used to delimit nodes within a component hierarchy path.
static PATH_NODE_DELIMITER: char = '/';

// Character used to escape interperetation of this parser's "special
// characers"; *, /, :, and \.
static ESCAPE_CHARACTER: char = '\\';

// Pattern used to encode wildcard.
pub static WILDCARD_SYMBOL_STR: &str = "*";
pub static WILDCARD_SYMBOL_CHAR: char = '*';

// Pattern used to encode globs.
static GLOB_SYMBOL: &str = "**";

// Globs will match everything along a moniker, but won't match empty strings.
static GLOB_REGEX_EQUIVALENT: &str = ".+";

// Wildcards will match anything except for an unescaped slash, since their match
// only extends to a single moniker "node".
static WILDCARD_REGEX_EQUIVALENT: &str = r#"(\\/|[^/])+"#;

/// Validates a string pattern used in either a PropertySelector or a
/// PathSelectorNode.
/// string patterns:
///    1) Require that the string not be empty.
///    2) Require that the string not contain any
///       glob symbols.
///    3) Require that any escape characters have a matching character they
///       are escaping.
///    4) Require that there are no unescaped selector delimiters, `:`,
///       or unescaped path delimiters, `/`.
fn validate_string_pattern(string_pattern: &String) -> Result<(), Error> {
    lazy_static! {
        static ref STRING_PATTERN_VALIDATOR: RegexSet = RegexSet::new(&[
            // No glob expressions allowed.
            r#"([^\\]\*\*|^\*\*)"#,
            // No unescaped selector delimiters allowed.
            r#"([^\\]:|^:)"#,
            // No unescaped path delimiters allowed.
            r#"([^\\]/|^/)"#,
        ]).unwrap();
    }
    if string_pattern.is_empty() {
        return Err(format_err!("String patterns cannot be empty."));
    }

    let validator_matches = STRING_PATTERN_VALIDATOR.matches(string_pattern);
    if !validator_matches.matched_any() {
        return Ok(());
    } else {
        let mut error_string =
            format!("String pattern {} failed verification: ", string_pattern).to_string();
        if validator_matches.matched(0) {
            error_string.push_str("\n A string pattern cannot contain unescaped glob patterns.");
        }
        if validator_matches.matched(1) {
            error_string
                .push_str("\n A string pattern cannot contain unescaped selector delimiters, `:`.");
        }
        if validator_matches.matched(2) {
            error_string
                .push_str("\n A string pattern cannot contain unescaped path delimiters, `/`.");
        }
        return Err(format_err!("{}", error_string));
    }
}

/// Validates all PathSelectorNodes within `path_selection_vector`.
/// PathSelectorNodes:
///     1) Require that all elements of the vector are valid per
///        Selectors::validate_string_pattern specification.
///     2) Require a non-empty vector.
fn validate_path_selection_vector(
    path_selection_vector: &Vec<StringSelector>,
) -> Result<(), Error> {
    for path_selection_node in path_selection_vector {
        match path_selection_node {
            StringSelector::StringPattern(pattern) => match validate_string_pattern(pattern) {
                Ok(_) => {}
                Err(e) => {
                    return Err(e);
                }
            },
            StringSelector::ExactMatch(_) => {
                //TODO(4601): What do we need to validate against exact matches?
            }
            _ => {
                return Err(format_err!(
                    "PathSelectionNodes must be string patterns or pattern matches"
                ))
            }
        }
    }
    Ok(())
}

/// Validates a TreeSelector:
/// TreeSelectors:
///    1) Require a present node_path selector field.
///    2) Require that all entries within the node_path are valid per
///       Selectors::validate_node_path specification.
///    3) Require that the target_properties field, if it is present,
///       is valid per Selectors::validate_string_pattern specification.
pub fn validate_tree_selector(tree_selector: &TreeSelector) -> Result<(), Error> {
    match &tree_selector.node_path {
        Some(node_path) => {
            if node_path.is_empty() {
                return Err(format_err!("Tree selectors must have non-empty node_path vector."));
            }
            validate_path_selection_vector(node_path)?;
        }

        None => {
            return Err(format_err!(
                "Missing node_path selectors are not valid syntax. You must provide a matcher
 describing the path from a hierarchies *root* to the node(s) of interest."
            ));
        }
    }

    match &tree_selector.target_properties {
        Some(target_properties) => match target_properties {
            StringSelector::StringPattern(pattern) => match validate_string_pattern(pattern) {
                Ok(_) => {}
                Err(e) => {
                    return Err(e);
                }
            },
            StringSelector::ExactMatch(_) => {
                // TODO(4601): What do we need to validate for exact match strings?
            }
            _ => {
                return Err(format_err!(
                    "target_properties must be either string patterns or exact matches."
                ))
            }
        },
        None => {
            return Err(format_err!(
                "Empty target properties are not valid syntax. You must provide a matcher
 describing the propert(y|ies) you wish to retrieve from the nodes you have selected."
            ));
        }
    }

    Ok(())
}

/// Validates a ComponentSelector:
/// ComponentSelectors:
///    1) Require a present component_moniker field.
///    2) Require that all entries within the component_moniker vector are valid per
///       Selectors::validate_node_path specification.
pub fn validate_component_selector(component_selector: &ComponentSelector) -> Result<(), Error> {
    match &component_selector.moniker_segments {
        Some(moniker) => {
            if moniker.is_empty() {
                return Err(format_err!(
                    "Component selectors must have non-empty moniker segment vector."
                ));
            }

            return validate_path_selection_vector(moniker);
        }
        None => return Err(format_err!("Component selectors must have a moniker_segment.")),
    }
}

pub fn validate_selector(selector: &Selector) -> Result<(), Error> {
    validate_component_selector(&selector.component_selector)?;
    validate_tree_selector(&selector.tree_selector)?;
    Ok(())
}

/// Parse a string into a FIDL StringSelector structure.
fn convert_string_to_string_selector(string_to_convert: &String) -> Result<StringSelector, Error> {
    validate_string_pattern(string_to_convert)?;

    // TODO(4601): Expose the ability to parse selectors from string into "exact_match" mode.
    Ok(StringSelector::StringPattern(string_to_convert.to_string()))
}

/// Increments the CharIndices iterator and updates the token builder
/// in order to avoid processing characters being escaped by the selector.
fn handle_escaped_char(
    token_builder: &mut String,
    selection_iter: &mut std::str::CharIndices<'_>,
) -> Result<(), Error> {
    token_builder.push(ESCAPE_CHARACTER);
    let escaped_char_option: Option<(usize, char)> = selection_iter.next();
    match escaped_char_option {
        Some((_, escaped_char)) => token_builder.push(escaped_char),
        None => {
            return Err(format_err!(
                "Selecter fails verification due to unmatched escape character",
            ));
        }
    }
    Ok(())
}

/// Converts a string into a vector of string tokens representing the unparsed
/// string delimited by the provided delimiter, excluded escaped delimiters.
pub fn tokenize_string(untokenized_selector: &str, delimiter: char) -> Result<Vec<String>, Error> {
    let mut token_aggregator = Vec::new();
    let mut curr_token_builder: String = String::new();
    let mut unparsed_selector_iter = untokenized_selector.char_indices();

    while let Some((_, selector_char)) = unparsed_selector_iter.next() {
        match selector_char {
            escape if escape == ESCAPE_CHARACTER => {
                handle_escaped_char(&mut curr_token_builder, &mut unparsed_selector_iter)?;
            }
            selector_delimiter if selector_delimiter == delimiter => {
                if curr_token_builder.is_empty() {
                    return Err(format_err!(
                        "Cannot have empty strings delimited by {}",
                        delimiter
                    ));
                }
                token_aggregator.push(curr_token_builder);
                curr_token_builder = String::new();
            }
            _ => curr_token_builder.push(selector_char),
        }
    }

    // Push the last section of the selector into the aggregator since we don't delimit the
    // end of the selector.
    if curr_token_builder.is_empty() {
        return Err(format_err!("Cannot have empty strings delimited by {}", delimiter));
    }

    token_aggregator.push(curr_token_builder);
    return Ok(token_aggregator);
}

/// Converts an unparsed component selector string into a ComponentSelector.
pub fn parse_component_selector(
    unparsed_component_selector: &String,
) -> Result<ComponentSelector, Error> {
    if unparsed_component_selector.is_empty() {
        return Err(format_err!("ComponentSelector must have atleast one path node.",));
    }

    let tokenized_component_selector =
        tokenize_string(unparsed_component_selector, PATH_NODE_DELIMITER)?;

    let mut component_selector: ComponentSelector = ComponentSelector::empty();

    // Convert every token of the component hierarchy into a PathSelectionNode.
    let path_node_vector = tokenized_component_selector
        .iter()
        .map(|node_string| convert_string_to_string_selector(node_string))
        .collect::<Result<Vec<StringSelector>, Error>>()?;

    validate_path_selection_vector(&path_node_vector)?;

    component_selector.moniker_segments = Some(path_node_vector);
    return Ok(component_selector);
}

/// Converts an unparsed node path selector and an unparsed property selector into
/// a TreeSelector.
pub fn parse_tree_selector(
    unparsed_node_path: &String,
    unparsed_property_selector: &String,
) -> Result<TreeSelector, Error> {
    let mut tree_selector: TreeSelector = TreeSelector::empty();

    if unparsed_node_path.is_empty() {
        tree_selector.node_path = None;
    } else {
        tree_selector.node_path = Some(
            tokenize_string(unparsed_node_path, PATH_NODE_DELIMITER)?
                .iter()
                .map(|node_string| convert_string_to_string_selector(node_string))
                .collect::<Result<Vec<StringSelector>, Error>>()?,
        );
    }

    tree_selector.target_properties =
        Some(convert_string_to_string_selector(unparsed_property_selector)?);

    validate_tree_selector(&tree_selector)?;
    return Ok(tree_selector);
}

/// Converts an unparsed Inspect selector into a ComponentSelector and TreeSelector.
pub fn parse_selector(unparsed_selector: &str) -> Result<Selector, Error> {
    // Tokenize the selector by `:` char in order to process each subselector separately.
    let selector_sections = tokenize_string(unparsed_selector, SELECTOR_DELIMITER)?;

    match selector_sections.as_slice() {
        [component_selector, inspect_node_selector, property_selector] => Ok(Selector {
            component_selector: parse_component_selector(component_selector)?,
            tree_selector: parse_tree_selector(inspect_node_selector, property_selector)?,
        }),
        _ => {
            Err(format_err!("Selector format requires exactly 3 subselectors delimited by a `:`.",))
        }
    }
}

pub fn parse_selector_file(selector_file: &Path) -> Result<Vec<Selector>, Error> {
    let selector_file = match fs::File::open(selector_file) {
        Ok(file) => file,
        Err(_) => return Err(format_err!("Failed to open selector file at configured path.",)),
    };
    let mut selector_vec = Vec::new();
    let reader = BufReader::new(selector_file);
    for line in reader.lines() {
        match line {
            Ok(line) => selector_vec.push(parse_selector(&line)?),
            Err(_) => {
                return Err(
                    format_err!("Failed to read line of selector file at configured path.",),
                )
            }
        }
    }
    Ok(selector_vec)
}

pub fn parse_selectors(selector_path: impl Into<PathBuf>) -> Result<Vec<Selector>, Error> {
    let selector_directory_path: PathBuf = selector_path.into();
    let mut selector_vec: Vec<Selector> = Vec::new();
    for entry in fs::read_dir(selector_directory_path)? {
        let entry = entry?;
        if entry.path().is_dir() {
            return Err(format_err!("Static selector directories are expected to be flat.",));
        } else {
            selector_vec.append(&mut parse_selector_file(&entry.path())?);
        }
    }
    Ok(selector_vec)
}

/// Helper method for converting ExactMatch StringSelectors to regex. We must
/// escape all special characters on the behalf of the selector author when converting
/// exact matches to regex.
fn is_special_character(character: char) -> bool {
    character == ESCAPE_CHARACTER
        || character == PATH_NODE_DELIMITER
        || character == SELECTOR_DELIMITER
        || character == WILDCARD_SYMBOL_CHAR
}

/// Converts a single character from a StringSelector into a format that allows it
/// selected for as a literal character in regular expression. This means that all
/// characters in the selector string, which are also regex meta characters, end up
/// being escaped.
fn convert_single_character_to_regex(token_builder: &mut String, character: char) {
    if regex_syntax::is_meta_character(character) {
        token_builder.push(ESCAPE_CHARACTER);
    }
    token_builder.push(character);
}

/// When the regular expression converter encounters a `\` escape character
/// in the selector string, it needs to express that escape in the regular expression.
/// The regular expression needs to match both the literal backslash and whatever character
/// is being `escaped` in the selector string. So this method converts a selector string
/// like `\:` into `\\:`.
// TODO(4601): Should we validate that the only characters being "escaped" in our
//             selector strings are characters that have special syntax in our selector
//             DSL?
fn convert_escaped_char_to_regex(
    token_builder: &mut String,
    selection_iter: &mut std::str::CharIndices<'_>,
) -> Result<(), Error> {
    // We have to push an additional escape for escape characters
    // since the `\` has significance in Regex that we need to escape
    // in order to have literal matching on the backslash.
    token_builder.push(ESCAPE_CHARACTER);
    token_builder.push(ESCAPE_CHARACTER);
    let escaped_char_option: Option<(usize, char)> = selection_iter.next();
    escaped_char_option
        .map(|(_, escaped_char)| convert_single_character_to_regex(token_builder, escaped_char))
        .ok_or(format_err!("Selecter fails verification due to unmatched escape character"))
}

/// Converts a single StringSelector into a regular expression.
///
/// If the StringSelector is a StringPattern, it interperets `\` characters
/// as escape characters that prevent `*` characters from being evaluated as pattern
/// matchers.
///
/// If the StringSelector is an ExactMatch, it will "santiize" the exact match to
/// align with the format of sanitized text from the system. The resulting regex will
/// be a literal matcher for escape-characters followed by special characters in the
/// selector lanaguage.
pub fn convert_string_selector_to_regex(
    node: &StringSelector,
    wildcard_symbol_replacement: &str,
) -> Result<String, Error> {
    match node {
        StringSelector::StringPattern(string_pattern) => {
            if string_pattern == WILDCARD_SYMBOL_STR {
                Ok(wildcard_symbol_replacement.to_string())
            } else {
                let mut node_regex_builder = "(".to_string();
                let mut node_iter = string_pattern.as_str().char_indices();
                while let Some((_, selector_char)) = node_iter.next() {
                    if selector_char == ESCAPE_CHARACTER {
                        convert_escaped_char_to_regex(&mut node_regex_builder, &mut node_iter)?
                    } else if selector_char == WILDCARD_SYMBOL_CHAR {
                        node_regex_builder.push_str(wildcard_symbol_replacement);
                    } else {
                        convert_single_character_to_regex(&mut node_regex_builder, selector_char);
                    }
                }
                node_regex_builder.push_str(")");
                Ok(node_regex_builder)
            }
        }
        StringSelector::ExactMatch(string_pattern) => {
            let mut node_regex_builder = "(".to_string();
            let mut node_iter = string_pattern.as_str().char_indices();
            while let Some((_, selector_char)) = node_iter.next() {
                if is_special_character(selector_char) {
                    // In ExactMatch mode, we assume that the client wants
                    // their series of strings to be a literal match for the
                    // sanitized strings on the system. The sanitized strings
                    // are formed by escaping all special characters, so we do
                    // the same here.
                    node_regex_builder.push(ESCAPE_CHARACTER);
                    node_regex_builder.push(ESCAPE_CHARACTER);
                }
                convert_single_character_to_regex(&mut node_regex_builder, selector_char);
            }
            node_regex_builder.push_str(")");
            Ok(node_regex_builder)
        }
        _ => unreachable!("no expected alternative variants of the path selection node."),
    }
}

/// Converts a vector of StringSelectors into a single regular expression which matches
/// strings encoding paths.
///
/// NOTE: The resulting regular expression makes the assumption that all "nodes" in the
/// strings encoding paths that it will match against have been sanitized by the
/// sanitize_string_for_selectors API in this crate.
pub fn convert_path_selector_to_regex(selector: &[StringSelector]) -> Result<Regex, Error> {
    let mut regex_string = "^".to_string();
    for path_selector in selector {
        // Path selectors replace wildcards with a regex that only extends to the next
        // unescaped '/' character, since we want each node to only be applied to one level
        // of the path.
        let node_regex =
            convert_string_selector_to_regex(path_selector, WILDCARD_REGEX_EQUIVALENT)?;
        regex_string.push_str(&node_regex);
        regex_string.push_str("/");
    }
    regex_string.push_str("$");

    Ok(Regex::new(&regex_string)?)
}

/// Converts a single StringSelectors into a  regular expression which matches
/// strings encoding a property name on a node.
///
/// NOTE: The resulting regular expression makes the assumption that the property names
/// that it will match against have been sanitized by the  sanitize_string_for_selectors API in
/// this crate.
pub fn convert_property_selector_to_regex(selector: &StringSelector) -> Result<Regex, Error> {
    let mut regex_string = "^".to_string();

    // Property selectors replace wildcards with GLOB like behavior since there is no
    // concept of levels/depth to properties.
    let property_regex = convert_string_selector_to_regex(selector, GLOB_REGEX_EQUIVALENT)?;
    regex_string.push_str(&property_regex);

    regex_string.push_str("$");

    Ok(Regex::new(&regex_string)?)
}

/// Sanitizes raw strings from the system such that they align with the
/// special-character and escaping semantics of the Selector format.
///
/// Sanitization escapes the known special characters in the selector language.
///
/// NOTE: All strings must be sanitized before being evaluated by
///       selectors in regex form.
pub fn sanitize_string_for_selectors(node: &str) -> String {
    let mut sanitized_string = String::new();

    let mut node_iter = node.char_indices();
    while let Some((_, node_char)) = node_iter.next() {
        if is_special_character(node_char) {
            sanitized_string.push(ESCAPE_CHARACTER);
        }
        sanitized_string.push(node_char);
    }

    sanitized_string
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::fs::File;
    use std::io::prelude::*;
    use tempfile::TempDir;

    #[test]
    fn canonical_component_selector_test() {
        let test_vector: Vec<(String, StringSelector, StringSelector, StringSelector)> = vec![
            (
                "a/b/c".to_string(),
                StringSelector::StringPattern("a".to_string()),
                StringSelector::StringPattern("b".to_string()),
                StringSelector::StringPattern("c".to_string()),
            ),
            (
                "a/*/c".to_string(),
                StringSelector::StringPattern("a".to_string()),
                StringSelector::StringPattern("*".to_string()),
                StringSelector::StringPattern("c".to_string()),
            ),
            (
                "a/b*/c".to_string(),
                StringSelector::StringPattern("a".to_string()),
                StringSelector::StringPattern("b*".to_string()),
                StringSelector::StringPattern("c".to_string()),
            ),
            (
                r#"a/b\*/c"#.to_string(),
                StringSelector::StringPattern("a".to_string()),
                StringSelector::StringPattern(r#"b\*"#.to_string()),
                StringSelector::StringPattern("c".to_string()),
            ),
            (
                r#"a/\*/c"#.to_string(),
                StringSelector::StringPattern("a".to_string()),
                StringSelector::StringPattern(r#"\*"#.to_string()),
                StringSelector::StringPattern("c".to_string()),
            ),
        ];

        for (test_string, first_path_node, second_path_node, target_component) in test_vector {
            let component_selector = parse_component_selector(&test_string).unwrap();

            match component_selector.moniker_segments.as_ref().unwrap().as_slice() {
                [first, second, third] => {
                    assert_eq!(*first, first_path_node);
                    assert_eq!(*second, second_path_node);
                    assert_eq!(*third, target_component);
                }
                _ => unreachable!(),
            }
        }
    }

    #[test]
    fn missing_path_component_selector_test() {
        let component_selector_string = "c";
        let component_selector =
            parse_component_selector(&component_selector_string.to_string()).unwrap();
        let mut path_vec = component_selector.moniker_segments.unwrap();
        assert_eq!(path_vec.pop(), Some(StringSelector::StringPattern("c".to_string())));

        assert!(path_vec.is_empty());
    }

    #[test]
    fn path_components_have_spaces_as_names_selector_test() {
        let component_selector_string = " ";
        let component_selector =
            parse_component_selector(&component_selector_string.to_string()).unwrap();
        let mut path_vec = component_selector.moniker_segments.unwrap();
        assert_eq!(path_vec.pop(), Some(StringSelector::StringPattern(" ".to_string())));

        assert!(path_vec.is_empty());
    }

    #[test]
    fn errorful_component_selector_test() {
        let test_vector: Vec<String> = vec![
            "".to_string(),
            "a\\".to_string(),
            r#"a/b***/c"#.to_string(),
            r#"a/***/c"#.to_string(),
            r#"a/**/c"#.to_string(),
        ];
        for test_string in test_vector {
            let component_selector_result = parse_component_selector(&test_string);
            assert!(component_selector_result.is_err());
        }
    }

    #[test]
    fn canonical_tree_selector_test() {
        let test_vector: Vec<(
            String,
            String,
            StringSelector,
            StringSelector,
            Option<StringSelector>,
        )> = vec![
            (
                "a/b".to_string(),
                "c".to_string(),
                StringSelector::StringPattern("a".to_string()),
                StringSelector::StringPattern("b".to_string()),
                Some(StringSelector::StringPattern("c".to_string())),
            ),
            (
                "a/*".to_string(),
                "c".to_string(),
                StringSelector::StringPattern("a".to_string()),
                StringSelector::StringPattern("*".to_string()),
                Some(StringSelector::StringPattern("c".to_string())),
            ),
            (
                "a/b".to_string(),
                "*".to_string(),
                StringSelector::StringPattern("a".to_string()),
                StringSelector::StringPattern("b".to_string()),
                Some(StringSelector::StringPattern("*".to_string())),
            ),
        ];

        for (
            test_node_path,
            test_target_property,
            first_path_node,
            second_path_node,
            parsed_property,
        ) in test_vector
        {
            let tree_selector =
                parse_tree_selector(&test_node_path, &test_target_property).unwrap();
            assert_eq!(tree_selector.target_properties, parsed_property);
            match tree_selector.node_path.as_ref().unwrap().as_slice() {
                [first, second] => {
                    assert_eq!(*first, first_path_node);
                    assert_eq!(*second, second_path_node);
                }
                _ => unreachable!(),
            }
        }
    }

    #[test]
    fn errorful_tree_selector_test() {
        let test_vector: Vec<(String, String)> = vec![
            // Not allowed due to empty property selector.
            ("a/b".to_string(), "".to_string()),
            // Not allowed due to glob property selector.
            ("a/b".to_string(), "**".to_string()),
            // Not allowed due to escape-char without a thing to escape.
            (r#"a/b\"#.to_string(), "c".to_string()),
            // String literals can't have globs.
            (r#"a/b**"#.to_string(), "c".to_string()),
            // Property selector string literals cant have globs.
            (r#"a/b"#.to_string(), "c**".to_string()),
            // Node path cant have globs.
            ("a/**".to_string(), "c".to_string()),
            ("".to_string(), "c".to_string()),
        ];
        for (test_nodepath, test_targetproperty) in test_vector {
            let tree_selector_result = parse_tree_selector(&test_nodepath, &test_targetproperty);
            assert!(tree_selector_result.is_err());
        }
    }

    #[test]
    fn successful_selector_parsing() {
        let tempdir = TempDir::new().expect("failed to create tmp dir");
        File::create(tempdir.path().join("a.txt"))
            .expect("create file")
            .write_all(b"a:b:c")
            .expect("writing test file");
        File::create(tempdir.path().join("b.txt"))
            .expect("create file")
            .write_all(b"a*/b:c/d/*:*")
            .expect("writing test file");

        assert!(parse_selectors(tempdir.path()).is_ok());
    }

    #[test]
    fn unsuccessful_selector_parsing_bad_selector() {
        let tempdir = TempDir::new().expect("failed to create tmp dir");
        File::create(tempdir.path().join("a.txt"))
            .expect("create file")
            .write_all(b"a:b:c")
            .expect("writing test file");
        File::create(tempdir.path().join("b.txt"))
            .expect("create file")
            .write_all(b"**:**:**")
            .expect("writing test file");

        assert!(parse_selectors(tempdir.path()).is_err());
    }

    #[test]
    fn unsuccessful_selector_parsing_nonflat_dir() {
        let tempdir = TempDir::new().expect("failed to create tmp dir");
        File::create(tempdir.path().join("a.txt"))
            .expect("create file")
            .write_all(b"a:b:c")
            .expect("writing test file");
        File::create(tempdir.path().join("b.txt"))
            .expect("create file")
            .write_all(b"**:**:**")
            .expect("writing test file");

        std::fs::create_dir_all(tempdir.path().join("nested")).expect("make nested");
        File::create(tempdir.path().join("nested/c.txt"))
            .expect("create file")
            .write_all(b"**:**:**")
            .expect("writing test file");
        assert!(parse_selectors(tempdir.path()).is_err());
    }

    #[test]
    fn canonical_path_regex_transpilation_test() {
        // Note: We provide the full selector syntax but this test is only transpiling
        // the node-path of the selector, and validating against that.
        let test_cases = vec![
            (r#"echo.cmx:a/*/c:*"#, vec!["a", "b", "c"]),
            (r#"echo.cmx:a/*/*/*/*/c:*"#, vec!["a", "b", "g", "e", "d", "c"]),
            (r#"echo.cmx:*/*/*/*/*/*/*:*"#, vec!["a", "b", "/c", "d", "e*", "f"]),
            (r#"echo.cmx:a/*/*/d/*/*:*"#, vec!["a", "b", "/c", "d", "e*", "f"]),
            (r#"echo.cmx:a/*/\/c/d/e\*/*:*"#, vec!["a", "b", "/c", "d", "e*", "f"]),
            (r#"echo.cmx:a/b*/c:*"#, vec!["a", "bob", "c"]),
        ];
        for (selector, string_to_match) in test_cases {
            let mut sanitized_node_path = string_to_match
                .iter()
                .map(|s| sanitize_string_for_selectors(s))
                .collect::<Vec<String>>()
                .join("/");
            // We must append a "/" because the absolute monikers end in slash and
            // hierarchy node paths don't, but we want to reuse the regex logic.
            sanitized_node_path.push('/');

            let parsed_selector = parse_selector(selector).unwrap();
            let tree_selector = parsed_selector.tree_selector;
            let node_path = tree_selector.node_path.unwrap();
            let selector_regex = convert_path_selector_to_regex(&node_path).unwrap();
            assert!(selector_regex.is_match(&sanitized_node_path));
        }
    }

    #[test]
    fn failing_path_regex_transpilation_test() {
        // Note: We provide the full selector syntax but this test is only transpiling
        // the node-path of the tree selector, and valdating against that.
        let test_cases = vec![
            // Failing because it's missing a required "d" directory node in the string.
            (r#"echo.cmx:a/*/d/*/f:*"#, vec!["a", "b", "c", "e", "f"]),
            // Failing because the match string doesnt end at the c node.
            (r#"echo.cmx:a/*/*/*/*/*/c:*"#, vec!["a", "b", "g", "e", "d", "f"]),
        ];
        for (selector, string_to_match) in test_cases {
            let mut sanitized_node_path = string_to_match
                .iter()
                .map(|s| sanitize_string_for_selectors(s))
                .collect::<Vec<String>>()
                .join("/");
            // We must append a "/" because the absolute monikers end in slash and
            // hierarchy node paths don't, but we want to reuse the regex logic.
            sanitized_node_path.push('/');

            let parsed_selector = parse_selector(selector).unwrap();
            let tree_selector = parsed_selector.tree_selector;
            let node_path = tree_selector.node_path.unwrap();
            let selector_regex = convert_path_selector_to_regex(&node_path).unwrap();
            assert!(!selector_regex.is_match(&sanitized_node_path));
        }
    }

    #[test]
    fn canonical_property_regex_transpilation_test() {
        // Note: We provide the full selector syntax but this test is only transpiling
        // the property of the selector, and validating against that.
        let test_cases = vec![
            (r#"echo.cmx:a:*"#, r#"a"#),
            (r#"echo.cmx:a:bob"#, r#"bob"#),
            (r#"echo.cmx:a:b*"#, r#"bob"#),
            (r#"echo.cmx:a:\*"#, r#"*"#),
        ];
        for (selector, string_to_match) in test_cases {
            let parsed_selector = parse_selector(selector).unwrap();
            let tree_selector = parsed_selector.tree_selector;
            let property_selector = tree_selector.target_properties.unwrap();
            let selector_regex = convert_property_selector_to_regex(&property_selector).unwrap();
            assert!(selector_regex.is_match(&sanitize_string_for_selectors(string_to_match)));
        }
    }

    #[test]
    fn failing_property_regex_transpilation_test() {
        // Note: We provide the full selector syntax but this test is only transpiling
        // the node-path of the tree selector, and valdating against that.
        let test_cases = vec![
            (r#"echo.cmx:a:c"#, r#"d"#),
            (r#"echo.cmx:a:bob"#, r#"thebob"#),
            (r#"echo.cmx:a:c"#, r#"cdog"#),
        ];
        for (selector, string_to_match) in test_cases {
            let parsed_selector = parse_selector(selector).unwrap();
            let tree_selector = parsed_selector.tree_selector;
            let target_properties = tree_selector.target_properties.unwrap();
            let selector_regex = convert_property_selector_to_regex(&target_properties).unwrap();
            assert!(!selector_regex.is_match(&sanitize_string_for_selectors(string_to_match)));
        }
    }

    lazy_static! {
        static ref SHARED_PASSING_TEST_CASES: Vec<(Vec<&'static str>, &'static str)> = {
            vec![
                (vec![r#"abc"#, r#"def"#, r#"g"#], r#"bob"#),
                (vec![r#"\**"#], r#"\**"#),
                (vec![r#"\/"#], r#"\/"#),
                (vec![r#"\:"#], r#"\:"#),
                (vec![r#"asda\\\:"#], r#"a"#),
                (vec![r#"asda*"#], r#"a"#),
            ]
        };
        static ref SHARED_FAILING_TEST_CASES: Vec<(Vec<&'static str>, &'static str)> = {
            vec![
                // Globs aren't allowed in path nodes.
                (vec![r#"**"#], r#"a"#),
                // Slashes aren't allowed in path nodes.
                (vec![r#"/"#], r#"a"#),
                // Colons aren't allowed in path nodes.
                (vec![r#":"#], r#"a"#),
                // Checking that path nodes ending with offlimits
                // chars are still identified.
                (vec![r#"asdasd:"#], r#"a"#),
                (vec![r#"a**"#], r#"a"#),
                // Checking that path nodes starting with offlimits
                // chars are still identified.
                (vec![r#":asdasd"#], r#"a"#),
                (vec![r#"**a"#], r#"a"#),
                // Neither moniker segments nor node paths
                // are allowed to be empty.
                (vec![], r#"bob"#),
            ]
        };
    }

    #[test]
    fn tree_selector_validator_test() {
        let unique_failing_test_cases = vec![
            // All failing validators due to property selectors are
            // unique since the component validator doesnt look at them.
            (vec![r#"a"#], r#"**"#),
            (vec![r#"a"#], r#"/"#),
        ];

        fn create_tree_selector(node_path: &Vec<&str>, property: &str) -> TreeSelector {
            let mut tree_selector = TreeSelector::empty();
            tree_selector.node_path = Some(
                node_path
                    .iter()
                    .map(|path_node_str| StringSelector::StringPattern(path_node_str.to_string()))
                    .collect::<Vec<StringSelector>>(),
            );
            tree_selector.target_properties =
                Some(StringSelector::StringPattern(property.to_string()));
            tree_selector
        }

        for (node_path, property) in SHARED_PASSING_TEST_CASES.iter() {
            let tree_selector = create_tree_selector(node_path, property);
            assert!(validate_tree_selector(&tree_selector).is_ok());
        }

        for (node_path, property) in SHARED_FAILING_TEST_CASES.iter() {
            let tree_selector = create_tree_selector(node_path, property);
            assert!(
                validate_tree_selector(&tree_selector).is_err(),
                format!("Failed to validate tree selector: {:?}", tree_selector)
            );
        }

        for (node_path, property) in unique_failing_test_cases.iter() {
            let tree_selector = create_tree_selector(node_path, property);
            assert!(
                validate_tree_selector(&tree_selector).is_err(),
                format!("Failed to validate tree selector: {:?}", tree_selector)
            );
        }
    }

    #[test]
    fn component_selector_validator_test() {
        fn create_component_selector(component_moniker: &Vec<&str>) -> ComponentSelector {
            let mut component_selector = ComponentSelector::empty();
            component_selector.moniker_segments = Some(
                component_moniker
                    .into_iter()
                    .map(|path_node_str| StringSelector::StringPattern(path_node_str.to_string()))
                    .collect::<Vec<StringSelector>>(),
            );
            component_selector
        }

        for (component_moniker, _) in SHARED_PASSING_TEST_CASES.iter() {
            let component_selector = create_component_selector(component_moniker);

            assert!(validate_component_selector(&component_selector).is_ok());
        }

        for (component_moniker, _) in SHARED_FAILING_TEST_CASES.iter() {
            let component_selector = create_component_selector(component_moniker);

            assert!(
                validate_component_selector(&component_selector).is_err(),
                format!("Failed to validate component selector: {:?}", component_selector)
            );
        }
    }
}
