//! Corpus scheduling: LibAFL's minimizer scheduler, culled only when its top-rated map changes.
//!
//! `MinimizerScheduler::next` culls on every call: it walks every top-rated entry and hashes the
//! map indices of each favored testcase (hundreds of thousands of set inserts on a grown corpus),
//! once per scheduled corpus entry. Culling only adds `IsFavoredMetadata` marks, which are never
//! removed, so when the top-rated map has not changed since the last cull it is a no-op. The map
//! only changes when a testcase is added, replaced or removed: cull then, otherwise skip it.
use libafl::corpus::{Corpus, CorpusId, HasTestcase, Testcase};
use libafl::schedulers::minimizer::{IsFavoredMetadata, DEFAULT_SKIP_NON_FAVORED_PROB};
use libafl::schedulers::{MinimizerScheduler, RemovableScheduler, Scheduler, TestcasePenalty};
use libafl::state::{HasCorpus, HasRand};
use libafl::common::HasMetadata;
use libafl::observers::CanTrack;
use libafl::Error;
use libafl_bolts::rands::Rand;
use libafl_bolts::serdeany::SerdeAny;
use libafl_bolts::tuples::MatchName;
use libafl_bolts::AsIter;
use libafl_bolts::HasRefCnt;

pub struct LazyCull<CS, F, I, M, O> {
    inner: MinimizerScheduler<CS, F, I, M, O>,
    dirty: bool,
}

impl<CS, F, I, M, O> LazyCull<CS, F, I, M, O> {
    /// `inner` must use the default skip probability (`MinimizerScheduler::new`).
    pub fn new(inner: MinimizerScheduler<CS, F, I, M, O>) -> Self {
        Self { inner, dirty: true }
    }
}

impl<CS, F, I, M, O, S> Scheduler<I, S> for LazyCull<CS, F, I, M, O>
where
    CS: Scheduler<I, S> + RemovableScheduler<I, S>,
    F: TestcasePenalty<I, S>,
    M: for<'a> AsIter<'a, Item = usize> + SerdeAny + HasRefCnt,
    S: HasCorpus<I> + HasMetadata + HasRand + HasTestcase<I>,
    O: CanTrack,
{
    fn on_add(&mut self, state: &mut S, id: CorpusId) -> Result<(), Error> {
        self.dirty = true;
        self.inner.on_add(state, id)
    }

    fn on_evaluation<OT>(&mut self, state: &mut S, input: &I, observers: &OT) -> Result<(), Error>
    where
        OT: MatchName,
    {
        self.inner.on_evaluation(state, input, observers)
    }

    /// `MinimizerScheduler::next`, with the cull skipped while nothing changed.
    fn next(&mut self, state: &mut S) -> Result<CorpusId, Error> {
        if self.dirty {
            self.inner.cull(state)?;
            self.dirty = false;
        }
        let mut id = self.inner.base_mut().next(state)?;
        while !state.corpus().get(id)?.borrow().has_metadata::<IsFavoredMetadata>()
            && state.rand_mut().coinflip(DEFAULT_SKIP_NON_FAVORED_PROB)
        {
            id = self.inner.base_mut().next(state)?;
        }
        Ok(id)
    }

    fn set_current_scheduled(&mut self, state: &mut S, next_id: Option<CorpusId>) -> Result<(), Error> {
        self.inner.set_current_scheduled(state, next_id)
    }
}

impl<CS, F, I, M, O, S> RemovableScheduler<I, S> for LazyCull<CS, F, I, M, O>
where
    MinimizerScheduler<CS, F, I, M, O>: RemovableScheduler<I, S>,
{
    fn on_remove(&mut self, state: &mut S, id: CorpusId, testcase: &Option<Testcase<I>>) -> Result<(), Error> {
        self.dirty = true;
        self.inner.on_remove(state, id, testcase)
    }

    fn on_replace(&mut self, state: &mut S, id: CorpusId, prev: &Testcase<I>) -> Result<(), Error> {
        self.dirty = true;
        self.inner.on_replace(state, id, prev)
    }
}
