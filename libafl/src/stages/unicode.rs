//! Stages which analysis common to Unicode-style mutations

use alloc::{collections::VecDeque, rc::Rc, vec::Vec};
use core::marker::PhantomData;

use bitvec::{bitvec, vec::BitVec};
use libafl_bolts::{impl_serdeany, Error};
use serde::{Deserialize, Serialize};

use crate::{
    corpus::HasTestcase,
    inputs::{BytesInput, HasMutatorBytes},
    stages::{ExecutionDecision, Stage, StageResult},
    state::{HasCorpus, HasCurrentTestcase, State, UsesState},
    HasMetadata,
};

/// Metadata which stores the list of pre-computed string-like ranges in the input
#[derive(Debug, Default, Serialize, Deserialize, Clone)]
pub struct UnicodeIdentificationMetadata {
    ranges: Rc<Vec<(usize, BitVec)>>,
}

impl_serdeany!(UnicodeIdentificationMetadata);

impl UnicodeIdentificationMetadata {
    /// The list of pre-computed string-like ranges in the input
    #[must_use]
    pub fn ranges(&self) -> &Vec<(usize, BitVec)> {
        self.ranges.as_ref()
    }
}

pub(crate) fn extract_metadata(bytes: &[u8]) -> UnicodeIdentificationMetadata {
    let mut ranges = Vec::new();

    if !bytes.is_empty() {
        let mut queue = VecDeque::new();
        let mut visited = bitvec![0; bytes.len()];
        queue.push_back(0);

        while let Some(i) = queue.pop_front() {
            if i >= bytes.len() || visited[i] {
                // if we've already visited a particular entry, then we already know its range(s)
                continue;
            }
            visited.set(i, true); // we always visit the current entry
            let s = core::str::from_utf8(&bytes[i..]).unwrap_or_else(|e| {
                queue.push_back(i + e.valid_up_to() + 1); // push to the next region
                core::str::from_utf8(&bytes[i..][..e.valid_up_to()]).unwrap()
            });
            if !s.is_empty() {
                let mut entries = bitvec![0; s.bytes().len()];
                for (c_idx, _) in s.char_indices() {
                    entries.set(c_idx, true);
                    visited.set(i + c_idx, true);
                }
                for unset in entries.iter_zeros() {
                    // each unset index potentially represents a new UTF-8 start point
                    queue.push_back(unset);
                }
                ranges.push((i, entries));
            }
        }
    }

    UnicodeIdentificationMetadata {
        ranges: Rc::new(ranges),
    }
}

/// Stage which identifies potential strings in the provided input
#[derive(Debug)]
pub struct UnicodeIdentificationStage<S> {
    phantom: PhantomData<S>,
}

impl<S> Default for UnicodeIdentificationStage<S> {
    fn default() -> Self {
        Self::new()
    }
}

impl<S> UnicodeIdentificationStage<S> {
    /// Create a new instance of the string identification stage
    #[must_use]
    pub fn new() -> Self {
        Self {
            phantom: PhantomData,
        }
    }
}

impl<S> UsesState for UnicodeIdentificationStage<S>
where
    S: State,
{
    type State = S;
}

impl<S, E, EM, Z> Stage<E, EM, Z> for UnicodeIdentificationStage<S>
where
    S: HasTestcase<Input = BytesInput> + HasCorpus + State,
    E: UsesState<State = S>,
    EM: UsesState<State = S>,
    Z: UsesState<State = S>,
{
    fn perform(
        &mut self,
        _fuzzer: &mut Z,
        _executor: &mut E,
        state: &mut Self::State,
        _manager: &mut EM,
    ) -> Result<StageResult, Error> {
        let mut tc = state.current_testcase_mut()?;
        if tc.has_metadata::<UnicodeIdentificationMetadata>() {
            return Ok(StageResult::Success); // skip recompute
        }

        let input = tc.load_input(state.corpus())?;

        let bytes = input.bytes();
        let metadata = extract_metadata(bytes);
        tc.add_metadata(metadata);

        Ok(StageResult::Success)
    }

    #[inline]
    fn should_run(&mut self, _state: &mut Self::State) -> Result<ExecutionDecision, Error> {
        // Stage does not run the target. No reset helper needed.
        Ok(ExecutionDecision::Continue)
    }

    #[inline]
    fn clear_progress(&mut self, _state: &mut Self::State) -> Result<(), Error> {
        // Stage does not run the target. No reset helper needed.
        Ok(())
    }
}
