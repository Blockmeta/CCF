Consensus Protocols
===================

CCF supports multiple consensus protocols.

The default consensus protocol for CCF is Raft.

There is an option of enabling PBFT for the consensus protocol.

.. warning:: Currently CCF with PBFT is in development and should not be used in a production environment.

Raft Consensus Protocol
-----------------------

The Raft implementation in CCF provides Crash Fault Tolerance.

For more information on the Raft protocol please see the orignial `Raft paper <https://www.usenix.org/system/files/conference/atc14/atc14-paper-ongaro.pdf>`_.


PBFT Consensus Protocol
-----------------------

There is an option of enabling CCF with PBFT as a consensus protocol providing Byzantine Fault Tolerance.

For more information on the PBFT protocol please see the original `PBFT paper <http://pmg.csail.mit.edu/papers/osdi99.pdf>`_.

PBFT is still under development and should not be enabled in a production environment. Features to be completed and bugs are tracked under the `Complete ePBFT support in CCF <https://github.com/microsoft/CCF/milestone/4>`_ milestone.

There is an open research question of `node identity with Byzantine nodes <https://github.com/microsoft/CCF/issues/893>`_.

By default CCF runs with Raft. To run CCF with PBFT the ``--consensus pbft`` CLI argument must be provided when starting up the nodes (see :ref:`here <operators/start_network:Starting a New Network>` for starting up a newtork and nodes).